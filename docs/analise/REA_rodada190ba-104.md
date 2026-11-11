# Round 190ba, issue #104: the Plugin database as a folder the user arranges in the file manager

Issue #104 (esiowry): "FL Studio allows users to customize the Plugin Database by right-clicking the Generators and
Effects folders to reveal their locations in Windows Explorer. Because the Browser's folder structure mirrors the file
system, users can reorganize plugins to their liking", and the plugin menus should all draw from that one structure.

Evidence markers: [FILES] read from the FL Studio 2026 install and the user's FL data folder (read only, nothing
changed); [EXEC 166f] seen in FL in round 166f (REA_rodada166f.md, item 1); [REPORTER] stated in the issue, not checked
in FL this round (no GUI driving in this round); [CODE] LYNX's code.

## 1. FL Studio

- [FILES] FL's Plugin database is a folder of the user's FL data: `<Documents>\Image-Line\FL Studio\Presets\Plugin
  database` with `Effects\`, `Generators\`, `Installed\` and one `.nfo` beside each (`Effects.nfo`, ...).
  - `Effects\` and `Generators\` hold FL's category folders (Effects: Controller, Delay reverb, Distortion, Dynamics,
    Filter, Flanger, Gain, Misc, Patcher, Visual; Generators: Controller, Drum, MIDI, Misc, Patcher, Sampler, Synth
    classic, Synth special, Visual) and anything the user adds. A plugin is `<name>.fst` (its state, the plugin
    identified inside) with `<name>.nfo` (for FL's own: `Bitmap=<name>.png`) and often `<name>.png` (the picker's
    picture). Entries can sit directly in `Effects\` / `Generators\` too (the user's folder has seven such `.fst` files).
  - `Installed\Effects|Generators\<CLAP | Fruity | New | VST | VST3>\` holds one `.fst` + `.nfo` per plugin found by
    FL's scan; that `.nfo` names the plugin file (`ps_file_filename_0`, `ps_file_idstring_0`, vendor, bit size, file
    date and size) and its own place (`ps_presetfilename=%FLPluginDBPath%\Installed\Effects\CLAP\Micro.fst`).
    `Installed\VerifiedIDs.nfo` is FL's own list. On this PC: 1249 `.fst`, 1237 `.nfo`, 138 `.png`.
- [EXEC 166f] The mixer slot menu and the Channel Rack's add menu list the top folders of `Effects` / `Generators` as
  headings (Categories), with subfolders as submenus first; a folder made in the file manager (`ZZ Test166f\Sub A`)
  showed up there. The radio items Categories / Simple / Tree sit at the bottom of those menus.
- [REPORTER] The Browser's context menu on Generators / Effects opens the folder in Windows Explorer, and users move
  the entry files and make folders there.
- Not checked this round (FL's documented behaviour, to confirm in FL): a deleted entry is no longer listed in the
  menus and the picker, while the plugin stays under Installed and in "More plugins...".
- Not checked: when exactly FL reads the folder again (the 166f test saw the new folder without restarting FL), and
  whether FL's ADD menu in the menu bar shows the Categories / Simple / Tree items.

## 2. LYNX before

- [CODE] `plugincatalog::Folders` (round 166f): the user's folders, a folder per plugin key and the menu view, kept in
  the settings as text; the names of renamed plugins in `plugin-names.txt`. Nothing on disk to look at or arrange.
- [CODE] The Channel Rack's add / Insert / Replace menus, the mixer slot menus and the menu bar's Add menu already
  read one list (`quickpick::addGeneratorMenu`, `addFolderGroups`, round 170b), but only the first two had the
  Categories / Simple / Tree switch.
- [CODE] A defect found on the way: the database's folders were saved in `plugin-folders.txt`, the file that also
  holds the folders LYNX scans for plugins. Each writer replaced the file: changing a database folder emptied the list
  of scan folders (the file then existed without one path, and `pluginFolders()` returned nothing instead of the
  suggested folders), and changing the scan folders dropped the database's folders.

## 3. LYNX now

`core/PluginDbFolder.h` (no JUCE), `Documents/LYNX/Plugin database` (the place FL uses under its own Documents folder;
under a test profile `<profile>/Documents/LYNX/Plugin database`):

    Effects/<folder>/<name>.lynxplugin        the arrangement: what the menus, the picker and the Browser show
    Generators/<folder>/<name>.lynxplugin
    Installed/Effects|Generators/<format>/    every plugin LYNX has (LYNX, FL engines, VFX, VST3, VST, CLAP, ...),
                                              written again by LYNX whenever the list of plugins changes
    database.index                            which plugins were given an entry once
    Read me.txt                               what the folder is and what can be done in it

An entry is a small UTF-8 text file:

    LYNX plugin 1
    key=<the plugin's key: its identifier string, or lynx:fx:<English name> / lynx:gen:<English name>>
    name=<its own name>
    format=<LYNX | FL engines | VFX | VST3 | VST | CLAP | ...>
    kind=effect | generator
    file=<the file name LYNX gave it, without the extension>
    shown=<the name given in LYNX, when renamed there>

Only `key` identifies the plugin. A file whose name is no longer `file` was renamed in the file manager: its name is
the plugin's name in the menus (FL: the entry's file name is the name). Names are made safe for every file system
(`safeFileName`: `\ / : * ? " < > |` and control characters become `_`, no trailing dots or spaces, Windows' device
names get a `_`, 120 bytes at most); two plugins of one name in one folder get `name (VST3)`, then `name (2)`.

Rules (the tree is what counts):

| In the file manager | LYNX |
|---|---|
| a folder / subfolder made (up to 4 levels, deeper ones count as the fourth) | a folder of the Plugin database: Browser, menus (heading / submenu), picker tab |
| an entry moved to another folder | the plugin is listed there |
| an entry copied from `Installed` into a folder | a scanned VST / CLAP plugin is filed there (or a deleted one is back) |
| an entry renamed | the plugin's name in the menus and the Browser ("1_EQ": picked by typing 1) |
| an entry deleted | the plugin leaves the menus' folders and the picker; it stays in the Browser's own lists (LYNX SHARE, FL engines, Installed), in `Installed` and in the Plugin manager |
| a folder deleted | its entries count as deleted |
| an entry in two folders | the first path (A-Z) counts (LYNX keeps one folder per plugin; FL would list it twice) |
| an entry of a plugin that is not installed (any more) | left alone, as FL does; it takes its place again when the plugin is back |

- A built-in plugin gets its entry in its default folder (FL's category for FL's plugins, else "LYNX SHARE": round
  170b) the first time LYNX sees it; `database.index` remembers that, so a deleted entry is not made again. A default
  folder holding only what is there by default is not shown as one of the user's folders in the Browser (as before).
- A scanned plugin has an entry in the arrangement only where the user filed it (as before: the others are listed
  under Plugin › and the Browser's Installed).
- Changes made in LYNX (Browser: Move to folder, No folder, New / Rename / Delete folder, Rename..., Original name,
  Folders from FL Studio's Plugin database) are written to the tree at once (`plugindb::apply`), then the tree is
  read back. A folder is removed only if LYNX had read it before and nothing else is left in it.
- Read again (`quickpick::refreshPluginDatabase`): when a plugin menu, the Plugin picker or the Browser's Plugin
  database is built (at most every two seconds), before the Browser's context menu on it, and when LYNX comes back to
  the front from another application. Entry files are parsed once and again only when their time or size changes.
- First run (migration): no entries on disk and no index → the tree is made from the folders, places and renamed
  names of the settings (`plugin-folders.txt` or the new `plugin-database.txt`, `plugin-names.txt`) plus the built-in
  plugins' default folders. Favourites (`plugin-favourites.txt`) and the menu view are settings and stay where they
  were. The settings keep a copy of the folders (`plugin-database.txt`) and names.
- The database's folders now have their own settings file (`plugin-database.txt`); `plugin-folders.txt` is the scan
  folders' alone, and a file left with only database lines by 0.1.25 or older is no longer read as "no scan folders".

UI (FL's places):
- Browser › Plugin database, right-click on the database, Effects / Generators or one of the user's folders: **Open
  in the file manager** (the existing item of the Browser's file folders), opening that folder; on LYNX's own groups
  (LYNX SHARE, FL engines, Installed) it opens `Installed/Effects|Generators`.
- Menu bar › Add: the Categories / Simple / Tree items after the channel list, as in the Channel Rack's add menu and
  the mixer slot menu; the three menus read the same folders and the same view.

Choices made here (recommended option taken, for the coordinator):
1. LYNX's own entry format instead of FL's `.fst`: LYNX's built-in plugins and its keys have no FL state, and writing
   `.fst` files would invite mixing the two databases. Importing FL's arrangement stays the existing Browser item.
2. Deleting an entry hides a built-in plugin from the folder menus and the picker (FL's behaviour) instead of making
   it again; the Read me in the folder says how to bring it back.
3. The Browser keeps its own groups (LYNX SHARE, FL engines, VFX, Installed) beside the user's folders, as since
   round 142 / 166f; the default category folders appear on disk and in the menus, not as Browser folders.

## 4. Tests and proof

- `tests/PluginDb190baTests.inc` (6 tests, 76 checks; run with ASan + UBSan against `core/PluginDbFolder.cpp` and
  `core/PluginCatalog.cpp`, `/mnt/e/lynx-work/iss190ba/104/coretest.sh`): safe names (illegal characters, device
  names, Unicode, length), entry round trip, first run from the settings and a second run changing nothing, changes
  made in the file manager (new folders, moves, a copy from Installed, rename, delete, folder rename / delete, a new
  and a missing plugin), changes made in LYNX (move, name clash, rename and original name, folder rename / delete, a
  folder made outside meanwhile, a hidden plugin filed again, a folder name a file system cannot hold), odd trees
  (duplicates, wrong kind, broken file, entry at the top, too deep, unwritable root, lost index).
- Windows (cloud round build of 0de426c81, run 42971888579, "round build: success"; `LYNX.exe --snapshot` twice under
  a fresh test profile, no window): the first run made `Documents/LYNX/Plugin database` (374 entry files; Effects and
  Generators with FL's category folders and LYNX SHARE, Installed/Effects|Generators/LYNX, FL engines, VFX). Then, with
  plain file operations as in Explorer: `Effects/Dynamics` moved into a new `Effects/Image-Line`, `Fruity Limiter`
  renamed `1_Limiter`, `Effects/Gain/Fruity Balance` deleted, `3x Osc` moved into a new `Generators/My synths/Bass`.
  The second run's menus (`b-menus170b.png`): a heading IMAGE-LINE with the submenu Dynamics › and no DYNAMICS
  heading, GAIN without Fruity Balance, a heading MY SYNTHS with Bass ›; its Browser (`b-browser-plugins.png`) lists
  the folder My synths (1) under Generators. Pictures in `/mnt/e/lynx-work/iss190ba/104/shots/`.

## 5. Left open

- The Browser does not show the default category folders (Dynamics, Filter...) as folders; the menus and the picker do.
- One folder per plugin (FL lists a plugin in every folder it has an entry in).
- No pictures beside the entries (FL's `.png`); the picker keeps its own thumbnails.
- FL facts marked [REPORTER] / "not checked" above need a look in FL when a GUI round is allowed.
