# REA round 168e — renders back into the project, exports per track, Export master, Project bones

Source of the scope: docs/analise/REA_rodada168c-gaps.md rows M1–M4. Every finding below comes from FL Studio 2026's
own binaries (FL64.exe / FLEngine_x64.dll in the round 167g Ghidra project, headless decompilation, and the DFM
resources of TFruityLoopsMainForm / TShortcutsModule / TWAVRenderForm). Clean-room: behaviour only, no code or art.

## 1. FL's menus [DFM]

- **Patterns**: `PSPatQuickRenderMenu` "&Quick render as audio clip" (Tag 1), `PSPatRenderMenu` "Render as &audio
  clip..." (Tag 0), `PSPatRenderReplaceMenu` "Render and replace..." (Tag 2), one handler `PSPatQuickRenderMenuClick`.
- **File › Export**: Wave / MP3 / OGG / FLAC / MIDI / M4A / Last exported, separator, "Master..."
  (`FileExportMasterAction`, ShortCut 49229 = Ctrl+Alt+M, hint "Render the project to a file and master it"),
  separator, Upload, "&All playlist tracks" and "All playlist track &groups" (each ▸ From song start... / From track
  start... / Time selection...), Video, "-Project": Zipped loop package..., "Project &bones...", "Project &data files...".
- **Playlist › Tools**: "&Consolidate playlist selection" ▸ "&From selection start" (Ctrl+Alt+C, 49219) / "From &song
  start" (Ctrl+Shift+Alt+C, 57411) → `EERenderPLSelectionActionExecute`; "Export all playlist tracks" / "... track
  groups" (tags 0–5).

## 2. Behaviour [REA]

### 2.1 Pattern render (FUN_011884b0)
- "Warning | Pattern is empty, nothing to render." when the pattern has no notes or events (FUN_012d8d70).
- The render window (TWAVRenderForm) is opened with flags 0x34: the mode box set to *Pattern* and locked, the formats
  locked to WAV; Quick render adds flag 1 = OK is pressed at once (no window).
- The file is "<pattern name or 'Pattern N'>.wav" in FL's render folder (FUN_011880b0), made unique.
- After the render (FUN_01188230): a new audio clip channel from AudioTrack.fst, the sample loaded, the name made
  unique ("<name> #2", FUN_00fe87a0), the pattern's colour. FL also files it in the "Audio clips" filter group.
- "Render and replace" (FUN_01187fd0): every Playlist clip of that pattern becomes an audio clip of the new channel at
  the same place and length (a trimmed clip keeps its offset). Undo names "quick render pattern" / "replace pattern
  instances". With several patterns picked in the picker, FL renders each ("Empty patterns were ignored.").
- Render window defaults for these modes (FormShow FUN_00d1e180): input FX on, "Split mixer tracks" off, Acid
  markers off, "Trim PDC silence" on, "Show file" off; the format mask is only saved for the normal export (mode 0).

### 2.2 Consolidation core (FUN_012fc180)
- Track −1 = the Playlist selection (clips selected, flag 0x80, and unmuted); none → "Nothing selected | There is no
  unmuted playlist selection to consolidate.". A track that renders nothing → "Track is empty | This track doesn't
  produce audio (or is completely muted). There is nothing to consolidate.".
- Flag 0x20 includes the members of the track's group (FUN_012ef210).
- During the render every other clip is muted except automation channels (type 5).
- A pattern clip whose pattern has tempo events → "Can't consolidate | One of the selected pattern clips contain tempo
  events and cannot be consolidated.".
- Range: the time selection if there is one, else first..last of the clips; "From song start" starts at 0.
- File (FUN_012fbbd0): the given name or "<first clip's source> (consolidated)".wav, in the render folder or the
  given folder, unique. Render via FUN_011dcf60 (flags 0x1d0 | 1: no window).
- Replace (no flag 4): the rendered clips are muted; with a time selection they are first cut at both edges and only
  the inside parts muted. A new channel (AudioTrack.fst, unique name, colour of the first clip's source); a new track is
  inserted at the topmost track of the selection (FUN_012e62e0; the old one moves down, the new one takes its group),
  the clip goes at the range's start, and the old track gets the "grouped" flag. Undo "consolidate playlist selection"
  / "consolidate playlist track".
- "Consolidate this track" (FUN_012ea170): name "%s (consolidated)", flags 0x108; Shift held → "Select a folder to
  consolidate to".

### 2.3 Export all playlist tracks / groups (FUN_012fd7a0)
- "Can't export tracks while recording"; folder chooser "Select rendering folder" / "Select the folder where output
  should be rendered...".
- Time selection mode without one: nothing. The two other modes clear the time selection.
- Tracks 1..last; with groups the grouped members are skipped (their header renders them with flag 0x20).
- File: "<project file name without extension> - <track name or 'Track N'>"; the render window is shown for the first
  track only (flag 1 after the first success).
- "Nothing rendered | Nothing was rendered because there are no unmuted clips in any of the selected tracks.",
  "Tracks skipped | Some tracks were skipped because they didn't have audio or were completely muted.",
  "Error | Something went wrong while rendering playlist tracks."; the folder is opened when "show file" is on.

### 2.4 Export › Master... (FUN_00edf6a0 / FUN_00edf490)
- FL renders the project and sends it to **FL Cloud mastering** (a separate download: "Mastering | This feature needs
  to be downloaded...", "FL Cloud mastering v2 Windows"); "Can't master while recording"; the result is
  "<project> (mastered)".
- LYNX has no online service. Decision: LYNX's own mastering on the computer (§3.4).

### 2.5 Project bones (FUN_011edb50 / FUN_011ecca0)
- Folder chooser "Export project bones" / "Save project bones (channel / effect presets && scores) subdirectory in...",
  then a sub-folder; FL writes a hidden "<dir>.nfo" (browser tip), Channels\<filter group>\<channel>.fst,
  Effects\<plugin>.fst, Mixer\<insert>.fst, Scores\<pattern> - <channel>.fsc, Automation\<pattern> - <param>.fsc.

## 3. LYNX

### 3.1 Code
- `src/core/QuickRender.h/.cpp` (new): the plain logic — pattern renderable/name, unique channel names, the render
  copies of the song, `applyPatternRender`, consolidation plans (`planSelection`, `planTrack`, `makePlanRenderSong`,
  `applyConsolidation`), `exportTracks`, `trackExportName`, `ticksForSeconds`, `projectBones`.
- `src/core/Mastering.h/.cpp` (new): loudness to a target (ITU-R BS.1770 meter, core/Loudness.h) with the Apex
  Limiter in true-peak mode at −1 dBTP, a few passes to correct for what the limiter takes.
- `src/ui/ExportDialog.cpp`: the Export window in clip-render mode (range / loops / stems / formats locked as FL's
  FormShow sets them, button "Render", choices kept in clip-render-settings.txt), in master mode (a "Loudness" row
  with −9 / −11 / −14 / −16 / −23 LUFS), and the functions behind the menus.
- Menus: Patterns (three items after "Split by channel"), File › Export (Master..., All playlist tracks, All
  playlist track groups, Project bones...), the Playlist's Tools menu (Consolidate playlist selection ▸, Export all
  playlist tracks / groups). Keys: Ctrl+Alt+C, Ctrl+Shift+Alt+C, Ctrl+Alt+M.
- The old `trackmodes::makeConsolidateSong` / `applyConsolidate` (round 146a) are replaced by the new core.

### 3.2 Differences kept on purpose / not done
- The new channel is not put into an "Audio clips" filter group (LYNX's channel rack has no such group yet).
- Pattern render: the current pattern only (FL also renders several patterns picked in the picker).
- Project bones: LYNX writes Mixer\*.lynxtrack, Effects\*.lynxpreset and Scores\*.mid (its own formats). No Channels
  (LYNX has no channel preset format yet), no Automation scores, no .nfo.
- Master: local mastering instead of FL Cloud (LYNX-own).

### 3.3 Tests
`tests/Rea168eTests.inc`: pattern render names / empty, render-and-replace, selection plan (time selection, song
start, tempo events, muted tracks), apply (mute, split at the edges, new track, groups), exports per track and groups,
ticks for seconds, mastering (target ±0.5 LU, true peak ≤ −0.9 dBTP, silence untouched), project bones.

## Where I stopped

| Item | Reporter's words (168c gap doc) | FL evidence | LYNX before | LYNX now |
|---|---|---|---|---|
| M1 | Patterns › Quick render / Render as audio clip / Render and replace | §1, §2.1 [DFM][REA] | none | done (current pattern; no "Audio clips" group) |
| M2 | Export › All playlist tracks / track groups (song / track start / time selection) | §1, §2.3 | none | done |
| M3 | Export › Master… Ctrl+Alt+M; Project bones | §1, §2.4, §2.5 | none | Master: LYNX-own local mastering; Project bones: mixer / effects / scores in LYNX formats |
| M4 | Tools › Consolidate playlist selection, Ctrl+Alt+C / Ctrl+Shift+Alt+C | §1, §2.2 | track-level only | done (and "Consolidate this track" rebuilt on the same core) |

State (2026-11-02, paused by the coordinator for round 168w): origin/main (efb8344d) merged in; Linux core build +
all tests green (1 631 180 checks, 0 failures); the Windows build had one compile error in Project bones (fixed,
not rebuilt yet). Notes in coord/notes-0.1.23.md.

Next step: Windows build (OUT=build-win) → GUI check with a fresh profile and test mode (Patterns › Quick render /
Render as audio clip / Render and replace, Playlist Tools › Consolidate (Ctrl+Alt+C), File › Export › All playlist
tracks, Master... (Ctrl+Alt+M), Project bones...; screenshots looked at) → merge origin/main → land.sh.
Decisions (approved by the user, 2026-11-02): (1) Export › Master stays LYNX-own local mastering instead of FL
Cloud; (2) Project bones ships without channel presets / automation for now. Follow-up: add Channels\ and
Automation\ to Project bones once LYNX has a channel preset format and an automation score writer. No FL session was used
for an audio comparison of the renders.
