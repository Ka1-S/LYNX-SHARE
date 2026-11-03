# Round 168c — refreshed FL-vs-LYNX gap inventory for 0.1.23 (docs only)

Date: 2026-11-01. Starts from `REA_rodada165f.md` (technology inventory), `REA_rodada167f-gaps.md` (plugins),
`REA_rodada167g-gaps.md` / `REA_rodada167h-gaps.md` (.flp events, Options dialog), `REA_rodada167j-gaps.md` (menus,
shortcuts, render dialog) and the round docs 167k–167r. What landed since is marked from `git log` of main
(86a03295); what was never inventoried was read from FL Studio 2026's own binaries only.

Clean-room: Ghidra (headless, read-only, the analysed `FLEngine_x64.dll` project of round 167g), DFM form resources
and strings of `FLEngine_x64.dll`, FL's install folders and the 165 projects in FL's `Data` folder. Behaviour and
formats only; nothing of FL's code, art or data is copied. FL Studio was **not** launched. No third-party binary was
analysed (the two iZotope DLLs in FL's folder were identified from their version resource only).

Markers: **[DECOMP addr]** Ghidra pseudocode (loader `FUN_00f46490`, project saver `FUN_00f40840`, channel saver
`FUN_00f3e190`), **[DFM]** a form resource, **[STR]** a string in FL's binary, **[DATA]** FL's `Data` corpus (165
.flp), **[FILES]** FL's install folders, **[CODE]** LYNX source (grep), **[HYP]** meaning not proven yet.

Effort: S < ½ day, M ≈ 1 day, L > 1 day. Value: H = audible / visible on every imported project or a daily
workflow, M = some projects / a common action, L = rare or cosmetic.

Material (scripts, corpus counts): `~/.claude/jobs/f8789b49/tmp/r168c/` — `ev.py` (every event id of the corpus:
count, files, top values → `corpus.txt`), `ctx.py` (the 3 events before / 2 after each occurrence),
`unknown.txt` (corpus ids with no `ev…` constant in `src/flp/Flp.cpp`), `g.sh` + `scripts/` (Ghidra runner).

## 1. Landed since the last inventories (main 72339935 → 86a03295)

| Item (gap doc) | Landed in | Merge |
|---|---|---|
| Emphasis on FL's engine (167f #1) | 167f | bf594f49 / f29400ea |
| Transporter on FL's engine (167f #2) | 167n | f0cc644e |
| Wave Traveller scratch curve (167f) | 167f-wt | 4681d546 |
| Newtime / Newtone / Transmitter, Edison / Scratcher, Kepler Exo | 167b / 167c / 167d | d8bfe531, d38ee6ee, 4da8f8b7 |
| Main pitch 0x50, current pattern 0x43, PAT/SONG 0x09, open arrangement 0x64, show info 0x0A, project times 0xED, play truncated notes 0x1E (167g #1–6, #14) | 167g | 10f31d28 |
| Sampler channel events, Fast declick 0x28 (167g #7, 167h #1) | 167h | f3c2157c |
| Panning law (mixer + channels), Play truncated notes on transport, Reset plugins on transport, Select first channel with notes (167h #2, #10, #24 part) | 167j / 167k | ad46fb98, d2ad4494 |
| PR Shift/Ctrl+arrows, Ctrl+D, Alt+M / Shift+Alt+M; Clone Ctrl+Shift+C, Delete Ctrl+Shift+Del, Shift+F4 / Ctrl+F4; Export MP3 Ctrl+Shift+R, MIDI Ctrl+Shift+M; render "Enable insert effects" (167j) | 167j | ad46fb98 |
| Undo backwards Ctrl+Alt+Z + Alternate undo mode (167h #8 part, 167j); Playlist 7 / 8 / 9 clip pitch / reverse; time-marker keys; mixer F2 / S / Alt+R / Shift+Alt+R; Playlist Ctrl+Enter / Ctrl+Left / Ctrl+Right | 167k | d2ad4494 / c444cba0 |
| Sampler legacy precomputed effects, per-clip stretch (+0x40), export "Tempo information" + markers, Alt solo, channel lock 0x20, ghost 0x32, CC 123 on transport reset | 167l | d7d067c2 |
| Keyboard Controller in the rack list (PR #19), MIDI Out polish (#17), two-column submenus (#18), F-keys (#23), preset arrows (#24), SFZ (#22) | 167m / 167p / 167o | 5d3f5c17, 4a5f74fc, 86a03295 |
| Online Sessions design (LYNX-own) | 167r | de6da0a0 |

Running now (not repeated below): 167q mixer track presets + #25, 167t piano-roll vertical zoom (#26), 168a Send
project (online sessions P1), 168b Speech synthesizer + local Loop Starter.

## 2. Gap table

### 2.1 .flp events still unread (corpus: 165 projects of FL's `Data`)

`unknown.txt` lists 57 ids the corpus carries that `src/flp/Flp.cpp` has no constant for. Leaving out the FL 1–9
era note / step events (0x01–0x04, 0x10, 0x48, 0x5B, 0x5C: 1–4 projects, imported elsewhere or obsolete), the
licence ones (0x1C, 0x25, 0x36, 0xC8 = licensee name [DECOMP saver: first event]) and the always-default ones, these
remain:

| # | Event | FL behaviour (evidence) | LYNX state | Effort | Value | Round |
|---|---|---|---|---|---|---|
| E1 | **0x9B** channel u32 | written right after the channel's name and before its colour 0x80 on every channel [DATA ctx: `CB 9B 80` 4 005×]; loader stores it in the channel's +0x7c [DECOMP FUN_00f46490]; 1 312 of 7 007 channels ≠ 0 (75, 74, 30…): the **channel icon** index (same pairing as the insert's) — the 167h claim "0x9B does not occur in the demos" is wrong | ignored; LYNX channels have no icon | S (import) + M (UI) | M | 168d / 168h → **model + import + .lynx done in 168j (UI 168h)** (REA_rodada168j) |
| E2 | **0x5F** insert u32 | loader → mixer track record +0x14 [DECOMP FUN_00f46490], the field round 149 found Create group copying as the track **icon** [DECOMP FUN_012be6f0]; follows the insert colour 0x95 [DATA ctx: `93 95 5F` 780×]; 1 210 inserts in 48 projects | ignored; no mixer icons ("Change icon" missing, 167j) | S + M (UI) | M | 168d / 168h → **model + import + .lynx done in 168j (UI 168h)** (REA_rodada168j) |
| E3 | **0xEE** playlist track +8 | LYNX's own comment: "u32 index, u32 colour, **u32 icon**" [CODE src/flp/Flp.cpp:101] | icon not imported | S | M | 168d → **done in 168j** (REA_rodada168j) |
| E4 | **0x67** u16 | loader: "ABORT FLP_FXTrackCount ItemData out of range" when < 3 or > 502 [STR + DECOMP FUN_00f46490]: the **mixer track count** (127 = FL's 125 inserts + Master + current; 18 in 15 demos) | LYNX derives the insert count from the inserts present | S | L | 168d → **done in 168j (count − 2 inserts)** (REA_rodada168j) |
| E5 | **0xCA** text | saver writes it between the project texts (title 0xC2, genre 0xCE, author 0xCF, 0xA7) and comments 0xC3 [DECOMP saver, DATA ctx `CE CF CA` 117×]; 164 projects, 1–144 B: the **project data folder** path (Options › Project › Data folder, 167h #6) [HYP: matches the Project-info form's data-path control] | ignored; LYNX has no project data folder | S (import) + M (feature) | M | 168i → **[HYP] proven + stored in 168j (feature 168i)** (REA_rodada168j) |
| E6 | **0x9F** u32 | saver: the FL version record's +0x26 u16, right after the version text 0xC7 [DECOMP saver] = FL's **build number** (4720, 1702…) | ignored | S | L | 168d (Project info "Saved with FL x.y.z build n") → **traced in 168j: the loader ignores it** (REA_rodada168j) |
| E7 | **0xA4** pattern u32 | pattern record +0x44 [DECOMP loader + saver]; 0 in 754 patterns, **1536** (= 16 beats × 96 PPQ) in one → a fixed pattern length in ticks [HYP] | ignored (LYNX patterns are as long as their content / the rack's steps) | M (trace + import) | M | 168d → **proven + imported in 168j (pattern fixed length)** (REA_rodada168j) |
| E8 | 0x9D / 0x9E pattern u32 | pattern record +0x10 / +0x18 (−1 default; 0x9D 0 / 1 / 2, 0x9E up to 251) [DECOMP]; 35 projects | ignored; meaning not traced (candidates: pattern group, "Edit patterns in" target) | M | L | 168d → **168j: no writer found, left unread** (REA_rodada168j) |
| E9 | 0x19, 0x34, 0xAB, 0xFC / 0xFD pattern | +0x1c, +0xa4 (bool), +0xa8, +0xac (text), +0xb4 (48-byte list) [DECOMP]; 2–8 projects | ignored | M | L | 168d (trace only) → **168j: not traced (constant in the corpus)** (REA_rodada168j) |
| E10 | **0x2A** insert u8 | the mixer saver FUN_00f3d620 writes the track record's first byte (+0x00), the loader stores it back as a bool [DECOMP both]; 1 on 896 of 4 209 inserts | ignored | M (trace) | M [HYP: a per-insert switch every project writes] | 168d → **proven in 168j: the insert's "own colour" flag, imported** (REA_rodada168j) |
| E11 | **0xA6** insert u32 0…3 | the mixer saver writes track +0x30 right after +0x2f = 0xA5, the record source point [DECOMP FUN_00f3d620]; loader clamps to 0…3: 1 on 3 157 inserts, 3 on 123 | ignored | M (trace) | M [HYP: the disk-recording / input mode next to the record source] | 168d → **proven in 168j: Monitor external input (Off / When armed / Auto / On), imported** (REA_rodada168j) |
| E12 | 0x2B playlist track u8 | track +0x70; 0 → loader sets a default in +0x2c (167g #8); 823 of 27 000 | ignored | M | L | 168d → **proven in 168j: the track's "own colour" flag, imported** (REA_rodada168j) |
| E13 | 0x24 arrangement u8 | arrangement record +0x10, after its name 0xF1 [DECOMP saver/loader]; 0 or 91 | ignored | S | L | 168d → **168j: not traced** (REA_rodada168j) |
| E14a | **0x1D** u8 project | the global the mixer's PDC menu item "Automatic" toggles: `AutoPDCMenuClick` 0x129f0c0 [RTTI method table → Mem: its only writer besides the loader and the reset FUN_011d3790]; read by the PDC planner FUN_010ff3b0 (round 165f-mixer §1.3) [Mem]; 1 in 159 of 165 projects [DATA] | ignored; LYNX's PDC is always automatic, so the 6 projects with 0 (PDC off) play compensated | S (+1 FL render of a 0 project) | M | 168d (+ the menu item in 168h) → **done in 168j (engine + mixer menu)** (REA_rodada168j) |
| E14b | **0x27** u8 project | the global of the mixer's PDC item "Compensate automations": `CompensateAutomationsMenuClick` 0x129efb0 [RTTI → Mem]; read by the mixer's per-slot processing next to the plugin's latency (FUN_01046a70, FUN_012b5e60…) [Mem, DECOMP r167h/r4790.c 21310]; 1 in 52, 0 in 21 projects | ignored (LYNX does not shift automation by plugin latency) | M | L–M | 168h → **done in 168j (engine + mixer menu)** (REA_rodada168j) |
| E14c | **0x23** u8 project | written by `TimeSettingComboChange` 0x13659d0 [RTTI → Mem], the Options › Project combo `timesettingcombo` with the items "Set as time division" ("Original FL Studio system") / "Set as time signature" ("Traditional time signatures") [DFM TMIDIForm]; read by `TimeDivision_Update` 0xeccbd0; 1 (= time signature) in 80 projects | ignored; LYNX shows "beats per bar" only (with 0x12 the denominator, 167g #11) | M | L–M | 168i → **done in 168j (Settings › Project combo)** (REA_rodada168j) |
| E14d | 0x2F, 0x85, 0xF3 project | 0x2F = byte +0x1b of the song object; 0x85 = a u32 got from the main-form object by FUN_00609830 (every project, 110…958); 0xF3 = the two u32 globals of the editors' Scale menu: `EEMenuScaleClick` 0x11f87e0 takes the item's tag and the second global (clamped ≥ 0) through FUN_00eae670 into the editor's scale (FUN_00e90670), the first one is a root / mode index (−1…−3 or < the list count, FUN_00e370c0) [RTTI → DECOMP] → the Piano Roll's **scale highlighting** (root + scale) saved with the project | ignored | M | L–M | 168d (trace) / 168g (scale) → **0xF3 proven + stored in 168j (FL scale table for 168g); 0x2F / 0x85 nothing to import** (REA_rodada168j) |
| E15 | 0xAA channel u32 | saver: the channel's +0x714 as a plain u32 (not a string list as 167l wrote) [DECOMP FUN_00f3e190]; 0 or −1 in 1 003 channels of 15 projects | ignored | S | L | 168d → **168j: no reader found** (REA_rodada168j) |
| E16 | 0x30, 0x33 channel; 0x2D / 0x2E / 0xA8 / 0x65 marker fields | always 0 in the corpus [DATA] | ignored | — | — | nothing to import |
| E17 | 0xE2 / 0xE6 second link list | 167g #10 / 167l §5 | ignored | M | L | not planned |
| E18 | 0x26 audio-track monitoring | 167l §5 | ignored until audio tracks monitor input | M | M | 168h (with input monitoring) |

### 2.2 FL install content LYNX does not use yet [FILES]

| # | Item | FL behaviour (evidence) | LYNX state | Effort | Value | Round |
|---|---|---|---|---|---|---|
| F1 | **FL's project templates** | `Data/Templates`: 35 .flp in Advanced 13, Minimal 5, Utility 8 (Create a chord progression, Launch loop starter, Remix a song, Record Audio, Vocoder, Drum mix, Surround mix, SFX transitions), Other 4, Performance 3, Empty, Power User [FILES]; File › New from template lists them | LYNX lists only its own templates (`core/Templates.h`) | S | M | 168i |
| F2 | Envelope presets (.fnv) | `Data/Patches/Envelopes` 16 files; [STR] "Denoiser - default.fnv" | no .fnv reader | S | L | later |
| F3 | Name presets | `System/Config/NamePresets_{Channel,MixerSlot,MixerTrack,Pattern,PlaylistTrack}.txt`: the rename dialog's preset names [FILES] | LYNX's rename dialog has no presets | S | L | 168h |
| F4 | Note colour presets, typing-to-piano layouts | `System/Config/Note color presets`, `Typing to piano` [FILES] | not read | S | L | 168g |
| F5 | **MIDI controller scripts** | `System/Config/Hardware specific`: 39 device folders (Akai, Novation, Korg, Arturia…) of Python `device_*.py` scripts, `Shared/Python` (CPython runtime) [FILES]; Options › MIDI "Controller type" + "Update MIDI scripts" [DFM] | LYNX has pocketpy (VFX Script, Edison and Piano Roll scripts) but no MIDI device-script API | L | M–H (controller owners) | decision §4 |
| F6 | FL Studio Remote | `Shared/ILRemoteServer_x64.dll`, `System/Config/FL Studio Remote Scripts` [FILES] | none | — | — | out of scope (§4) |

### 2.3 Menus, editors and windows (167j rows still open + new)

| # | Item | FL behaviour (evidence) | LYNX state [CODE grep] | Effort | Value | Round |
|---|---|---|---|---|---|---|
| M1 | **Patterns › Quick render / Render as audio clip / Render and replace** | Patterns menu [DFM 167j] | none | M | H | 168e |
| M2 | **Export › All playlist tracks / track groups** (song start / track start / time selection) | File › Export [DFM 167j] | none (mixer-insert stems exist) | M | H | 168e |
| M3 | Export › Master… Ctrl+Alt+M; Project bones | [DFM 167j] | none | M | L–M | 168e |
| M4 | **Tools › Consolidate playlist selection** (from selection / song start), Ctrl+Alt+C / Ctrl+Shift+Alt+C | [DFM 167j] | track-level "Consolidate track" only | M | M | 168e |
| M5 | Playlist track menu: Lock to content, Lock to this size, Send pattern instances here, Auto name clips, Change icon, Select audio input / Arm / Disarm | PLTrackMenu [DFM 167j] | none of these | M | M | 168f (icon: 168h) |
| M6 | Playlist Tools: Lock / Unlock all tracks to content, Reset empty tracks, Collapse / Expand all grouped tracks, Purge unused audio clips, Switch all / selected audio clip stretch modes | [DFM 167j] | none | M | L–M | 168f |
| M7 | Playlist Edit (FL storage: "Create variants when stretching" = one global flipped by `CreateVariantsMenuClick` 0xe3a090; "Lock to content" = byte +0x19 of the playlist track record (0x114 B per track), undo name "lock/unlock to content" (`EEPLTrackClipLockMenuClick` 0x12236c0 → FUN_012e7a90); "Lock to this size" = +0x10 size with the sign of +0x14 (`EEPLTrackSize1MenuClick` 0x1228f10) [RTTI → DECOMP]): Delete all, Trim selection, Insert / Delete space items, Turn into automation clip, Allow clip nudging, Dropping locks to content, Create new clips with manual fades, Snap fade handles, Create variants when stretching | [DFM 167j, TEventEditForm "Create variants when stretching audio clips"] | keys only for insert/delete space; the rest missing | M | M | 168f |
| M8 | Playlist keys: Ctrl+G merge pattern clips, Ctrl+Shift+G / Ctrl+Alt+G merge similar / automation clips, Ctrl+Shift+B clone tracks, Ctrl+Shift+V make unique, Shift+6 zoom on performance clips | [DFM ShortcutsModule 167j] | menu items without keys or missing | S | M | 168f |
| M9 | Playlist view: Hide collapsed grouped tracks, fade / gain previews, mini playlist preview, precise time indicator, continuous autoscroll, marker "Move content left / right" | [DFM 167j] | none | M | L | 168f |
| M10 | **Piano Roll: Show chord name for selection**, note grid highlights Alt+B, scale highlighting, background waveform Alt+N | [DFM 167j] | none (scale snap / fold exist) | M | M | 168g |
| M11 | Piano Roll File: Open / Save / Browse score (.fsc), Paste from / Copy to MIDI clipboard, Import Ctrl+M / Export Ctrl+Shift+M keys | [DFM 167j]; 609 .fsc in `Data/Patches/Scores` [FILES] | LYNX reads .fsc for its tools only (`core/PianoRollTools`), no File items, no keys | M | M | 168g |
| M12 | Piano Roll keys: Shift+M / Shift+O select more random / odd, Shift+N stamp none, Ctrl+Alt+Ins slice insert space, Ctrl+I insert current controller value; Edit › Delete all, Shift / Transpose items | [DFM 167j] | menu items without keys or missing | S | M | 168g |
| M13 | Piano Roll pattern-level time markers; Note properties window compare (TPRNotePropForm: VEL, REL, PAN, MOD X/Y, PITCH, REPEAT, colour, portamento, slide) | [DFM 165f §1.9 "never compared"] | inspector exists, never compared | M | L–M | 168g |
| M14 | Export as score sheet (PDF) | TExportScoreForm [DFM] | none | L | L | decision §4 |
| M15 | **Options › Step edit Ctrl+E, Blend recorded notes Ctrl+B, Loop record, Tools › Cancel recording** | main menu [DFM 167j] | Overdub item without key; no step edit, no cancel | M | M–H | 168i |
| M16 | Options › Multilink to controllers Ctrl+J, Enable groups Shift+Alt+G | [DFM 167j] | none | M | M | 168i |
| M17 | View › Close all unfocused windows Ctrl+F12, Align all channel editors Shift+F12, Realign Ctrl+Shift+H, Touch controller Alt+F7, Project picker Ctrl+F8 | [DFM ShortcutsModule 167o table] | none of these keys | S | L | 168i |
| M18 | **Mixer: Change icon, Gradient color, Assign to / Unassign from new audio track(s)** | mixer track menu [DFM 167j] | none | M | M | 168h |
| M19 | Mixer Disk recording submenu: Latency compensation, Ignore silent takes, Auto name playlist tracks, Group loop recorded takes, Apply track level and mute, format 16 / 24 / 32 | [DFM 167j] | not in the mixer menu (some exist elsewhere) | M | M | 168h |
| M20 | Mixer PDC: Set for all tracks Ctrl+P, Automatic, Compensate automations; Link all parameters… | [DFM 167j] | "Reset manual latency on all tracks" only | M | L | 168h |
| M21 | Mixer View: Waveforms Alt+W, Names at top, Routing cables, compact plugin list, Colorful mixer, external input levels; Open audio editor Ctrl+E / and record Shift+E | [DFM 167j] | LYNX's own view items | M | L | 168h |
| M22 | **Sampler "Variants" pool** (list of the channel's audio variants, memory use; menu Turn into audio clip channel / Save sample as… / Preview) | TPluginForm `VariantsList` "Variants of the channel's audio", `VariantPoolMenu` [DFM] | none | M | M | later (after 168f's "Create variants when stretching") |
| M23 | Make unique as sample; audio clip holds its last sample value as DC to the clip's end | [DFM 167j]; 167l §Where I stopped [FL render] | missing; LYNX ends at the sample's end | S–M | M (bit-exact renders) | 168d (DC hold, engine) / 168f → **DC hold done in 168j** (REA_rodada168j) |

### 2.4 Options dialog (167h rows still open)

| # | Item | Evidence | LYNX [CODE grep: no caption] | Effort | Value | Round |
|---|---|---|---|---|---|---|
| O1 | Create automation clips at the playback position | [DFM] `createautoclipsatcursorbox` | missing | S | M | 168i |
| O2 | Monophonic painting in piano roll | [DFM] `prpaintmodebox` | missing | S | L | 168i |
| O3 | Restore previous state after solo | [DFM] `remembermutestatebox` | missing | S | L | 168i |
| O4 | Auto name channels / effect slots; Auto zip empty channels | [DFM] | missing | S | L | 168i |
| O5 | Detach all new plugins, Open detached plugins with the FL window, Hide plugin toolbar, Limit open plugin editors (25), Disable typing-keyboard priority | [DFM] | missing | M | L | 168i |
| O6 | Undo knob tweaks, Put undone recordings in the recycle bin, Show recent changes at the top | [DFM] | missing (alternate undo done in 167k) | M | M | 168i |
| O7 | MIDI: Foot pedal controls note off, Toggle on release, velocity link curves, Auto accept detected controller | [DFM] | missing | M | L | 168i |
| O8 | Read sample tempo / root note, Fast sample preview, Auto keep long audio on disk | [DFM] | fixed behaviour, not compared | S | L | 168i |
| O9 | Force high performance power plan | [DFM] `hppowerplanbox` | missing | S | L | decision §4 |
| O10 | Copy used samples to the project folder; Data folder (+ 0xCA) | [DFM] TNewProjForm / project page | missing | M | M | 168i |

### 2.5 FL features behind a download, a licence or an online service [STR] [FILES]

| # | Item | FL behaviour (evidence) | LYNX state | Round |
|---|---|---|---|---|
| A1 | Extract stems from sample (TDemucsForm) | [STR] "il.Demucs", "Extract stems\|This feature needs to be downloaded before it can be used.", `/FL Studio Optional Features/Stem extractor Windows.zip` | issue #27: LordVersA builds it (ONNX Runtime loaded at run time, model downloaded on first use; the owner's comment agreed) | contributor (§4) |
| A2 | **Remix a song** | [FILES] it is a Utility *template* (`Data/Templates/Utility/Remix a song/Remix a song.flp`, nfo: "Split music into vocals, instruments bass and drums."), listed in Tools [STR "Remix a song"]; it needs A1 | none | after #27 + F1 |
| A3 | Declipper, Denoiser, Vocal denoiser, Deverb | [STR] "Declipper\|This feature needs to be downloaded…", `Edison Declipper Windows.zip`, "Deverb\|This feature is not available in trial."; FL ships iZotope's Audio Restore (`AudioRestore.dll` "Audio Restore (VST)", `iZAudioRestore.dll` "Audio Restore (Core)", CompanyName iZotope, Inc. — version resource only) | LYNX's own DSP versions in the audio editor (Clean up: denoise with a noise profile, declipper, declicker; Vocal denoise; Deverb — round 165l, `core/AudioEditTools.h`) | decision §4 |
| A4 | FL Cloud (sounds, mastering, distribution, backup, plugins), Gopher, in-app shop, Upload / YouTube / SoundCloud, FL Studio Remote | [DFM 167j "Not planned"], `ILRemoteServer_x64.dll` | none | out of scope (§4) |
| A5 | Loop Starter's FL Cloud content | 165f §1.5 | 168b builds a local one | 168b |

## 3. Prioritized plan: the next six rounds

Each round owns its files, so they can run in parallel; the order is the priority when only two run at a time.

1. **168e — Render and export (L, value H).** M1 Quick render / Render as audio clip / Render and replace, M2
   Export all playlist tracks / track groups (song start / track start / time selection), M4 Consolidate playlist
   selection (+ keys), M3 Export master Ctrl+Alt+M, Project bones. Files: `engine/Exporter.*`, `core/ExportOptions.*`,
   `ui/ExportDialog.*`, a new `core/QuickRender.*`; only the menu entries in `MainComponent.cpp`. Proof: FL renders of
   the same project (`FL64.exe /R` under the locks) vs LYNX's files, sample-compare.
2. **168d — .flp leftovers and icon model (M, value M).** E1–E15: trace the [HYP] rows (0x2A, 0xA6, 0xA4, 0x9D/0x9E,
   0x2B, the project globals) through their readers, import what changes playback or display, keep the rest in the
   .lynx round trip; add `icon` to channels, inserts and playlist tracks (model + .lynx + import, no UI); the build
   number and the mixer track count; M23 the audio clip's DC hold to the clip end (engine, FL render proof). Files:
   `src/flp/Flp.*`, `core/FlpImport.cpp`, `core/Song.h` + its .lynx serializer, `engine/` clip player, tests.
3. **168f — Playlist editor (M–L, value M).** M5–M9 (icons excluded), M7 "Create variants when stretching".
   Files: `ui/PlaylistComponent.*`, `core/KeyMap.cpp` (Playlist keys), `core/` playlist edit helpers. FL evidence:
   DFM + decompiled handlers per item, one planned FL session at most.
4. **168i — Recording, global options and templates (M–L, value M–H).** M15–M17, O1–O8, O10 + E5 (data folder),
   F1 FL's templates in New from template. Files: `ui/SettingsPages.cpp`, `ui/SettingsDialog.cpp`,
   `engine/AudioEngine` recording path, `core/Templates.*`, the Options / View / Tools menus of `MainComponent.cpp`
   (only those menus, so 168e's File › Export entries do not collide).
5. **168g — Piano Roll (M–L, value M; after 167t lands).** M10–M13 + F4. Files: `ui/PianoRollComponent.*`,
   `core/NoteEdit.*`, `core/PianoRollTools.*`, `ui/NoteToolDialogs.*`.
6. **168h — Mixer and icons UI (M–L, value M; after 167q lands).** M18–M21, F3 name presets, the icon picker for
   mixer tracks, playlist tracks and channels on 168d's model (LYNX's own glyph set mapped to FL's icon indices — no
   FL art), E18 input monitoring if audio-track monitoring lands. Files: `ui/MixerComponent.*`, the rename dialog,
   a new `ui/IconPicker.*`; touches the Channel Rack / Playlist headers only to draw the icon.

After these: M22 Sampler Variants, M14 score sheet, F5 MIDI controller scripts, the browser tags of 165f #10, FL
Slicer 2 / FL Studio Mobile / Fruity LSD (167f) — each waits for the decision in §4 where there is one.

## 4. Decisions for the user (pt-BR)

1. **Separar stems / "Remix a song" / "Extract stems".** No FL 2026 isso é um recurso baixado à parte (Demucs) e o
   "Remix a song" é só um template que usa os stems. O LordVersA vai fazer no #27 (ONNX Runtime carregado em tempo de
   execução, modelo baixado no primeiro uso). **Recomendação:** deixar com ele; depois que entrar, a gente adiciona o
   template "Remix a song" e o item no menu do clipe. Falta só você confirmar o modelo padrão: os pesos do HTDemucs
   têm licença pouco clara — recomendo oferecer o download mostrando a fonte e a licença, sem embutir no instalador.
2. **Declipper / Denoiser / Vocal denoiser / Deverb "com IA".** No FL eles são da iZotope (Audio Restore, licenciado)
   e baixados à parte; não dá pra copiar nem usar. O LYNX já tem versões próprias (DSP, sem IA) no editor de áudio.
   **Recomendação:** manter as nossas agora; quando o ONNX do #27 estiver no main, fazer um "Denoise (IA)" com um
   modelo aberto de licença clara (ex.: DeepFilterNet, MIT/Apache — a confirmar), baixado no primeiro uso.
3. **FL Cloud, Gopher, loja, upload pro YouTube/SoundCloud, FL Studio Remote.** São serviços online / app e protocolo
   fechados da Image-Line. **Recomendação:** fora do escopo (o LYNX tem as Online Sessions próprias).
4. **Scripts de controladores MIDI (os `device_*.py` do FL, 39 marcas na pasta Hardware specific).** Dá pra fazer a
   API de scripts do FL no Python embutido do LYNX (trabalho grande, 2+ rodadas). **Recomendação:** sim, rodando os
   scripts direto da instalação do FL do usuário (só leitura, nada copiado pro repositório), API escrita clean-room;
   começar pelos controladores mais comuns (Akai, Novation, Arturia).
5. **Exportar partitura em PDF (Export as score sheet).** Precisa de um gerador de PDF de notação. **Recomendação:**
   primeiro exportar MusicXML (sem dependência nova; o MuseScore abre e imprime), o PDF próprio depois.
6. **"Force high performance power plan" (Windows).** Muda o plano de energia do sistema. **Recomendação:** fazer como
   opção desligada por padrão e voltar o plano anterior ao fechar o LYNX.
7. **Fruity Slicer 2, FL Studio Mobile / Rack FX, Fruity LSD.** Motores grandes e quase sem uso nos projetos do FL
   (Slicer 2: só presets vazios; LSD precisa de um banco GM). **Recomendação:** esperar um projeto real de usuário
   que use; para o LSD, usar o player SoundFont do LYNX com um banco GM livre baixado no primeiro uso.

## 5. Where I stopped

- Done: §1–§4. Ghidra this round (read-only on the 167g FLEngine project): Mem on the project globals (E14a–d),
  Dec of the mixer saver FUN_00f3d620 (E2, E10, E11), the PDC / Time settings / Scale handlers, and the playlist
  track menu handlers (M5 / M7 storage). Handler names from the Delphi method tables (`r167h/py/mtab.py '.*'`,
  16 372 entries). Outputs: `~/.claude/jobs/f8789b49/tmp/r168c/` (m1.txt, d2.txt, d3.txt, allmeth.txt).
- Still [HYP]: E5 (0xCA = data folder), E7 (0xA4 = fixed pattern length), E8/E9 pattern fields, E10/E11 meanings
  (storage proven, the UI control not), E12 (0x2B), 0x2F / 0x85.
- Rerun: `cd ~/.claude/jobs/f8789b49/tmp/r168c && SCRIPT=Mem.java ./g.sh m1.txt <ptr hex…>` / `./g.sh d.txt <depth> <fn hex…>`.
- Next: round 168d takes E1–E18 from here; 168e–168i as in §3.
