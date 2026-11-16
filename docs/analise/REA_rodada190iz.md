# REA round 190iz — the Browser: FL's six tabs, tags, "Send to selected channel or focused plugin", the 5-second preview, Live's browser

Round 190iz (LYNX SHARE 0.1.30): BUILD-LIST 9 (cov190gf), pain point B14 (pain190gc), design slices 27 FM-S7 and 28 CL-J
(design190ia/PLAN.md, findings FM-8, FM-9, CL-22), details row R20 (details190if). Evidence first, then what LYNX does.

Sources:
- `M:` = FL manual, https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm (read 2026-11-16).
- `S:` = strings of FL Studio 2026's `FLEngine_x64.dll` (22,227,016 bytes, 2026-10-08), `strings -el`.
- `C:` = FL's own settings files on the study install: `Documents/Image-Line/FL Studio/Settings/Browser/Tab_*.tabconfig`.
- `R:` = reference shots `design190ia/shots/ref/fl-playlist.png` (FL) and `live-session.png` (Live 12.4).
- Live manual 12, chapter 4 "Managing Files and Sets" (`M-BR`, cited by details190if DL-97..104).

## §1 FL's tabs

| Evidence | What it shows |
|---|---|
| S: `PROJECT`, `PLUGINS`, `PRESETS`, `SOUNDS`, `STARRED` next to `UserTabs`, `UserTabs\`, `*.tabconfig`, `forms.browser` | FL's built-in tabs besides ALL |
| C: `Tab_ALL.tabconfig` (TabType=0), `Tab_PLUGINS` (1), `Tab_PROJECT` (8), `Tab_SOUNDS` (6); keys `TabIcon`, `TabName`, `FilterTags`, `LastTabIndex` | the tab state FL saves per tab |
| R: fl-playlist.png 0,104,205,140 | six icon tabs over the width: a waveform, two documents (selected, orange outline), a plug, a globe, a cloud, a star |
| M: "By default these include All, Current Project, Plugin Database, Online Content and Starred"; tab menu Rename / Hide / Clone / Move / "Save search result as tab" | the manual's (older) list; user tabs are FL-own management not built here |

LYNX: six icon tabs in FL's order ALL, PROJECT, PLUGINS, PRESETS, SOUNDS, STARRED (`core/BrowserPlaces.h tabOrder`),
icons drawn by LYNX (`ui/BrowserTagsLive.inc browsertabicons`), the selected one in a 30x30 box with a 1.5 px accent
outline (finding FM-8), names in the tooltip and the hint bar. PRESETS and SOUNDS are FL Cloud content in FL; LYNX has
no FL Cloud, so they list the preset places (FL synth / 3x Osc presets, the plugins' presets) and the sample places
(audio only). Tree rows 24 px with FL's dotted guides and a face one step larger (FM-9).

## §2 Tags (FL's Browser tagging)

| Evidence | What it shows |
|---|---|
| M: Search › "Tags - Shows / hides the available content Tags"; "Any / All (Tag search logic) - Will show content with 'Any' of the selected tags, or only that with 'All' the selected tags" | the search row's tags panel and its logic |
| M: "Add a tag - Type in a Tag to add to the file. You can add multiple Tags by separating them with a comma, e.g. typing - Kick,808,C# will give the content 3 tags"; "Edit tags - Edit any existing Tags"; "To remove existing tags select files and (Right+Click) > 'Delete' Tags" | the item menu |
| S: `.searchpanel.tagsbtn`, `.tagspanel.tagsgridpanel`, `.tagspanel.combinedtagsanybtn`, `.tagspanel.combinedtagsallbtn`, `\|Search by tags`, `TAGS`, `\|Show items with any tags`, `\|Show items with all tags`; menu `Tags`, `Favorite`, `Add tag`, `Edit tags`, `Import tags`; `FilterTags` (tab state) | the UI parts and the per-tab filter |
| S: `TTagDatabase`, "FL Studio was unable to read the tag database. Tags and Favourites cannot be changed." | one tag database of the user, which also holds the favourites — not the project |
| M: "Load plugin tags - When unchecked, plugins won't have any automatic tags assigned"; S: `Browser plugin factory tags` | automatic plugin tags (not built, see §7) |

LYNX (`core/BrowserTags.h`, tests `browser190iz_*`): any number of tags per file, folder or Plugin database entry;
"Add tag..." takes comma-separated names; "Tags ▸" ticks a tag on / off; "Edit tags..." renames (merging into an
existing tag of that name) or deletes a tag everywhere; the TAGS button beside the search box shows the tag chips with
Any / All; picked tags list the matching items (the rest of the search text must be in their names); Live's `#tag`
works in the search box too (M-BR 4.2). Stored once per user in the settings folder (`browser-tags.txt`), not in the
project; a renamed / moved path keeps its tags (`Store::moved`). Tag names keep the first spelling, match ignoring case.

## §3 Live's browser in the Clip-launch layout

| Evidence | What it shows |
|---|---|
| R: live-session.png 0..318, 86..800 (finding CL-22) | search row; sidebar 100 px with Collections (colour dots), Library (All, Sounds, Drums, Instruments, Audio Effects, MIDI Effects, Modulators, Max for Live, Plug-Ins, Clips, Samples, Grooves, Tunings, Templates), Places (Packs, Splice, Cloud, Push, User Library, Current Project, Add Folder...); rows 19 px, selected `#637e86`; content: tag chips (`#363636`, hover `#464646`) and a "Name" list |
| livebrowser190ce shots, round 190ce/190cy | Live's colour collections, already in LYNX |

LYNX: in the Clip-launch layout the Browser shows the sidebar beside the list; the Pattern and Simple layouts keep FL's
tabs and tree. Library entries map to LYNX's content (`core/BrowserPlaces.h`): Sounds = preset places, Drums = the sample
places searched for drum words, Instruments / Audio Effects / MIDI Effects (FL's VFX) / Plug-Ins (installed) /
Modulators (controllers) = Plugin database entries as one list, Clips = the Scores places, Samples = sample places,
Templates = the user's project templates; Max for Live, Grooves (the Groove Pool sits under the grid) and Tunings are
dimmed, as Live dims Cloud / Push when they are not there. Places: Packs (FL's packs when FL is installed), User Library
(LYNX's own preset / Speech / Clipboard places), Current Project, the Browser's folders, Add Folder.... Splice / Cloud /
Push are account-bound services LYNX does not have (left out). The tags panel is Live's chip area; the list has Live's
"Name" title and 19 px rows. Collection 1 shows as Live's "Favorites" there.

## §4 "Send to selected channel or focused plugin"

| Evidence | What it shows |
|---|---|
| M: File menu "Send to selected channel or focused plugins - Content will be sent to the selected Channel or Plugin as noted" | the item |
| REA round 143 B3 / B5 (EXEC br_file.png, br_plugin.png) | FL's first item on a sample and on a Plugin database entry |
| M: Actions: "Middle mouse button - Loads compatible content on the selected Channel"; "Enter - Replaces the channel with the selected item"; Shift+Up/Down | the other ways in |

LYNX (`core/BrowserSend.h decide`, test `browser190iz_send_to_selected_channel_or_focused_plugin`): a sample goes into
the selected channel when it plays samples (else a new Sampler channel, as Enter does); a SoundFont into the selected
Soundfont channel; a plugin preset into the focused plugin window's plugin (the last one used,
`PluginWindows::openPlugins`), else the selected channel's plugin; a Plugin database generator replaces the selected
channel's generator in one undo step (FL's Enter), an effect goes into the selected insert. The middle button does the
same on a row.

## §5 Preview length, Shift+Click, Live's click-to-seek

| Evidence | What it shows |
|---|---|
| M: Browser Menu › "By default audio files clicked in the Browser will play for 5 seconds"; Preview Player: "By default the preview will be up to 5 seconds unless Browser Menu > 'Full sample preview' is selected"; "Alt/Opt+Click plays a sample to the end"; "Shift+Click starts playback from the mouse position" | FL (details DF-91, DF-92; owner decision F2: FL's 5 s default) |
| M-BR 4.9: "click in the scrub area to make playback jump to that point" | Live (DL-99) |

LYNX: `browserpreview::range` (tests): 5 s of the file from where it starts, the whole file with "Full sample preview"
(Browser menu), Alt+Click or the strip's Play button; a capped preview fades out over 10 ms (no click at the cut);
Shift+Click on the strip (and a plain click in the Clip-launch layout) plays from the mouse position. The engine keeps
`previewFile (file)` whole for every other caller.

## §6 The favourite star

REA round 115 table #5 (hover star, "to do"); M: "Hover over a file and click the star that appears at the end of the
content"; "Deselect the star wherever you see it". LYNX: the hovered row shows an outline star at its end, a starred row a
filled one; a click toggles Starred (collection 1). The Clip-launch layout keeps Live's colour squares instead.

## §7 Not built (and why)

- FL's user tabs (Clone / Save search result as tab / Rename, colour and icon) and "Import tags": FL-own tab management,
  larger than this round; candidates for a later round.
- FL's automatic plugin tags ("Load plugin tags", factory tags): FL's own plugin categories; LYNX's Plugin database
  folders cover the grouping.
- Live's tag groups / Tag editor / Quick Tags (Live 12 filter view): needs Live's tag taxonomy; the FL tags cover the
  user side.
- Backspace / Ctrl+[ ] navigation history (DF-96 / DL-102), F3 next match (DF-97), Live's → / Shift+Enter preview
  (DL-98): key-map work; round 190iu is adding KeyMap actions this week (kept apart to avoid a merge clash).
