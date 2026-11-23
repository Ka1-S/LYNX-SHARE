# REA round 190oq — issues #243–#252 (ARA on an insert, search focus, moving clips, the rack in Track-only mode, Settings search, mute keys, delete fade, project packages, start page)

FL Studio 2026 (C:\Program Files\Image-Line\FL Studio 2026), Ableton Live 12.4 (manual), Bitwig / Studio One / REAPER /
Cubase / Logic (their manuals). Clean-room: behaviour only, no code or data copied. Work folder
`/mnt/e/lynx-work/issues243_190oq/` (`rea/FL-FACTS.md` holds the static-analysis evidence in full).

## 1. ARA on a mixer insert, and the musical context (#243)

### Where the hosts that support ARA put it

| Host | Placement | Notes |
|---|---|---|
| Cubase / Nuendo | Per audio event ("Extensions" on the event); an insert is a plain (non-ARA) effect | event level, as LYNX's clip menu (round 190kx) |
| Studio One | Per event ("Edit with Melodyne", an event effect) and on a track insert (all events of the track) | both levels |
| REAPER | Track FX (all items on the track) or take FX; ARA is on for an ARA plugin by default (FX menu switch) | track level by default |
| Logic Pro | First insert slot of an audio track: every region of the track | track level |
| Pro Tools | Track-level "ARA plug-in" on an audio track's first insert | track level |
| FL Studio 2026, Live 12 | No ARA (Melodyne runs as a plain VST3 effect, transfer mode) | round 190kx: no ARA string in FL's binaries |

LYNX before: event level only (clip menu › Edit in ARA plugin, per channel: FL's channel = the sample).
LYNX now (LYNX extra, as REAPER / Logic / Studio One): an ARA plugin in a mixer insert slot, `EffectSlot::ara` on, plays
and edits every audio clip of the audio clip channels routed to that mixer track. Implementation: `ara::projectInserts`
gives those channels the slot's plugin as their ARA link in the song the rack plays (the channel's own link wins) and
leaves the slot empty there, so the same tested per-channel path (one bound instance per channel, one document per plugin)
plays them; the slot's window opens the plugin's editor with every routed clip selected (each channel is one region
sequence = one track in Melodyne). The clips reach the insert's chain as if the plugin were its first effect (Logic's
rule). Bypassing the slot plays the clips unedited (A/B, as those hosts). One ARA slot per mixer track counts (the first).
A new ARA plugin put on an insert that audio clip channels are routed to starts in ARA mode (REAPER's default); the
Master and inserts without audio clips start as a plain effect; the slot menu switches it either way. Old projects and FL
imports: off (no "ara" key). 0.1.28 copies drop it with a loss line.

### The musical context ("tempo entries")

ARA 2's musical context carries the host's tempo map (kARAContentTypeTempoEntries: (seconds, quarters) pairs, the tempo
constant between two entries) and its bar signatures (kARAContentTypeBarSignatures: numerator, denominator, position in
quarters). Melodyne draws its bar ruler, its tempo and its quantize grid from them. LYNX before: two entries at the
project tempo and the project meter at 0 — right for a constant tempo, wrong as soon as the song has tempo automation or
time signature markers (Melodyne's bars drifted from LYNX's, and the regions were placed at the project tempo's seconds).
Now (`ara::timeMap`, `ara::tempoEntries`, `ara::barSignatures`): one entry per tempo change of the song (tempo automation
and pattern Tempo events in song mode, read per 1/16 step as playback and Song::secondsForTicks do), a bar signature per
time signature marker; the playback regions are placed through the same map, and the ARA instances' play head reports
the map's seconds (`AraPlayHead`), so region, ruler and transport agree. A constant-tempo song gives exactly the old
entries and play head (bit-identical).

## 2. Search boxes take the keyboard at once (#244)

| Fact | Evidence |
|---|---|
| FL's Plugin picker (F8, the Channel rack's +, a mixer slot) has no edit box: typing goes straight into its search ("start to type to search plugins....", up to 70 characters, Ctrl+V pastes) | [DECOMP FUN_00ec53b0, FUN_00ec7370; STR 0xabee74] |
| FL's Tools › Browser find (Ctrl+F) shows the Browser and puts the caret in its search | [DECOMP FUN_00f1c510] (partly) |

LYNX: its "Find an effect" (a mixer slot's double-click), Manage shortcuts, Quick-add palette and Plugin manager windows
asked for the focus before their window showed, so the request was lost (bug). `ui::focusOnOpen` marks the box; the tool
window gives it the focus once shown (and once more on the next message, as some systems activate the window later).

## 3. Moving clips up or down a track (#245), the Channel rack in Track-only mode (#246)

| Keys (Playlist, clips selected) | FL 2026 | Evidence |
|---|---|---|
| Shift+Up / Down | Edit › Shift up / Shift down: the selection (else all clips) one track | [DFM EEShiftUAction 8230, EEShiftDAction 8232] |
| Ctrl+Up / Down | select the previous / next Playlist track | [DECOMP FUN_00e52360 → FUN_012e83e0 modes 7/8] |
| Ctrl+Alt+Up / Down | add the track above / below to the selection | [DECOMP same, modes 9/10] |
| Ctrl+Shift+Up / Down | Patterns › Move up / down | [DFM PSPatUpMenu 24614] |
| Alt(+Shift)+Up / Down, Up / Down | scroll the view | [DECOMP FUN_00e52360] |

LYNX already has Shift+Up / Down (round 189e): nothing to change; the reporter's Alt+Shift keys scroll in FL.

Channel rack (FL 2026): the filter lists "All", a line, then every group A–Z; the built-in groups are "Audio" and
"Automation" (FL's loader renames an older project's "Audio clips" / "Automation clips", event 0xE7) and "Unsorted"
[DFM TStepSeqForm FilterSelect; DATA PTR_PTR_015e0b30; DECOMP FUN_00f46490, FUN_00fff0b0]. There is no instrument-track
filter; making an instrument track leaves the channel in its group: it stays in the rack [DECOMP FUN_00e71980]. No FL
option hides the rack. LYNX: Track-only mode (round 190iu, LYNX extra) hides the rack when it comes on, but F6 opens it
again (it refused before); the filter is FL 2026's list (names "Audio", A–Z order).

## 4. Settings search (#247)

FL 2026's Settings (TMIDIForm) have no search or edit control; no "search settings" string in FLEngine or FLMManaged
[DFM, STR]. LYNX extra: a find bar along the bottom of Settings (jump, outline, n of m, dimmed tabs).

## 5. Mute / solo keys (#249)

| Key | FL 2026 | Evidence |
|---|---|---|
| Alt+M / Shift+Alt+M (Playlist, Piano roll) | set the mute of the selection (in the time range); never toggle | [DFM EEMuteAction 32845 Tag 1, EEUnmuteAction 41037 Tag 0; DECOMP FUN_00e3cc70, FUN_00e3cb60] |
| Mixer S / Alt+S | solo / alt solo the selected tracks; again unsolos | [DFM TFXForm SoloMenu 83; DECOMP FUN_0127c2a0 mode 2] |
| Channel rack 1…0 / Ctrl+1…0 | toggle mute / solo of the Nth channel | [DECOMP FUN_01038660, FUN_00ffaaa0] |
| Mute toggles of a selection | only by mouse (Mute tool T, Right-Alt click, the clip menu's "Muted") | [DECOMP FUN_00e84110, FUN_01235020] |

LYNX matched all of these already. LYNX extra (off by default): "Alt+M toggles" (core/MuteKeys.h).

## 6. Deleted items fade out (#250); the Welcome window (#252)

FL's item flash FUN_00e8fc60 (level ≥ 2 of Options › General › Animations, FL's default "Make it pretty"): Cut / Delete
(FUN_00eac420) and the erase stroke (FUN_00e69050, the right-click delete and the Delete tool [INFER]) add a ghost per
item, in the Playlist and the Piano roll. Colour: the item's with HSL lightness clamped to 0.55..0.80 (FUN_00660dc0).
Opacity 1 → × exp(ln 0.01 · I / T) per animation tick (T 1000 ms Playlist, 500 ms Piano roll; dropped < 0.025: 801 / 401
ms). Geometry: the item's rectangle, grown by 4 px · (1 − a); corner min(½ min(w, h), 8 px); outline (3a + 0.5) px at
224a/255; fill (224a − 96)/255 while > 0; drawn above the items (FUN_00e88030). LYNX (ui/DeleteFade.h) draws the same,
gated by Settings › General › Animations (on by default) and Reduced effects.

FL's "Welcome to FL Studio" window (TWelcomeWizard): "Recent projects" (What's new, Other projects..., up to 50 recent
files: name, "<size> <date> (<ago>)" and the folder; ago = "seconds ago" / "%dm ago" / "%dh %dm ago" / "%d days ago"),
"Load a demo project", "Start a new … project"; shown at every start without a file until "Do not show this in the
future" (stored as ShowWelcomeWizard, default false) [DECOMP FUN_01144a10, FUN_011495e0, FUN_01148c60, FUN_011ef1c0].
LYNX's start page carries the same contents in LYNX's look; off by default pending the owner's decision.

## 7. Zipped loop package, Project data files, Collect (#251)

| FL 2026 | Behaviour | Evidence |
|---|---|---|
| File › Export › Zipped loop package... (no key); Save as with a .zip name ("Zipped song file") | the .flp (named after the zip) and every used file, flat, by name; duplicates: "Save anyway / Cancel" warning | [DFM FileExportZipAction; DECOMP FUN_011dc560, FUN_00ed0370] |
| File › Export › Project data files... | copies every used file flat into a chosen folder ("Copy the %d files this project is currently using to..."); nothing re-pointed, no .flp | [DECOMP FUN_011ec0f0] |
| File › Save to new project folder... | the .flp in an empty folder with Audio\ and Samples\ | [DECOMP FUN_011ec2c0] |
| Collect All and Save | none in FL (Live's File menu) | [STR] |

LYNX: Export › "Zipped loop package..." (was "Project with samples (.zip)", its files in "samples/"), now flat and named
after the zip (a second file of the same name gets a unique name instead of FL's warning); Save as takes a .zip name;
Export › "Project data files..." copies without re-pointing (was the re-pointing collect, which stays in Tools); File ›
"Collect all and save" (Live's command, LYNX extra): files outside the project's folder into Samples/Imported, used from
there, then saved.

## 8. Send-level automation from .flp (#253, bug)

| Fact | Evidence |
|---|---|
| A send's level is mixer control 0x2000 + destination (0 = Master), 0…16000 on the fader curve (12800 = 0 dB); before FL 24.2.99 0x1F40 + destination (FL converts on load) | [DECOMP FUN_00f45460, REA_rodada2 §3] |
| An automation clip's target (event 0xE3, flag 8) is that same control id | [REA_rodada2 §3 "it also serves to resolve automation targets"; 0xE3 records in the demos] |
| FL demos with send automation (byte scan of 0xE3 links, `s253/scan.py`): Synth_dfr - Overcome (insert 15 → 3 and 2, insert 12 → 4 and 5), LollieVox, Edlan & Ella Noël, Astes, Atthar, Bombs Away | [DATA] |
| One FL automation clip may drive two controls (two 0xE3 links for one channel: Synth_dfr's clips each drive two sends) | [DATA] |

LYNX: `automationTargetFor` had no case for 0x2000 + d, so those clips came in with no target (silent). Now
AutomationTargetKind::mixerSend {source, destination}; its playback (setRouteLevel at faderGain) and its lanes already
existed. Known limit (not changed): a clip linked to two controls keeps its first link (FL's second target is dropped).
