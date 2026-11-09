# Round 177c — vocal comping on take lanes (comp177c)

User-approved 2026-11-06 (STUDY.md gap #4; research-web-2.md row 5): record several takes, pick the best parts of
each, with automatic crossfades. FL's workflow stays the base; comping is an addition on top of FL's own "Group loop
recorded takes" lanes.

Evidence markers: [STR] strings in the vendor's own binary, [DOC] vendor manual, [WEB] users / reviews (URL),
[CODE] LYNX sources, [TEST] unit test, [RUN] measured in the app.

## 1. What existed

| Area | LYNX before (round 163c) | Where |
|---|---|---|
| Loop recording → one clip per pass | ✅ last pass plays on the take's track, earlier passes muted on grouped tracks under it ("<track> (take N)") | [CODE] `takes::placeTakeClips` |
| Comp group | ⚠️ implicit: a grouped track's header is its "comp track" | [CODE] `takes::compTrackOf` |
| Choosing a part of a take | ⚠️ right-click a lane clip › "Promote this take to the comp track" (whole clip or the loop region) | [CODE] PlaylistComponent, `takes::promoteTake` |
| Fades at a switch | ❌ butt splice with 8-tick linear fade-out + fade-in: a ~6 dB dip at every switch (no overlap) | [CODE] |
| The last pass as a lane | ❌ only on the comp track: once replaced it was gone (except undo) | [CODE] |
| Audition a lane | ❌ (un-mute clips by hand) | — |
| Which part of each take the comp uses | ❌ | — |
| Flatten | ❌ | — |
| More takes over an existing comp | ❌ went to a new track below everything | [CODE] `takes::takeTrack` |

## 2. FL Studio 2026 (v26.1.7 build 5654)

Added marker: [OBS] seen in FL's window and read back from the `.flp` FL saved. Saved test project:
`E:\lynx-work\comp177c\fl\takes-final.flp` with its WAVs in `fl\Project_1\Audio`; shots in `E:\lynx-work\comp177c\fl\`
(`c0x`, `c13c`, `c18c`, `d08c`, `e05c`, `f00`).

### 2.1 Observed [OBS]

Setup, every run: Song mode, a 2-bar Playlist loop region, record filter "Audio, into the playlist as an audio clip", FL
Studio ASIO In 1–In 2 on one insert named "REC", Recording starts playback on.

| Run | Loop record | Blend recorded notes | Passes | Tracks | Grouped (`.flp` 0xEE byte 46) | Clip flags |
|---|---|---|---|---|---|---|
| R1 | on | off | 1 (stopped early, 226 ticks) | 1 | no | 0x2040 muted |
| R2 | on | off | 4 (last 303 ticks) | 2–5 | 2 no, 3–5 yes | all 0x2040 muted |
| R3 | on | off | 2 (last 745 ticks) | 6–7 | 6 no, 7 yes | all 0x2040 muted |
| R4 | off | off | 1 (768 ticks, the full region) | 8 | no | 0x40 plays |
| R5 | on | on | 3 (last 223 ticks) | 9–11 | 9 no, 10–11 yes | all 0x40 play |

R0, before a machine reboot, was seen only on screen and its project was lost: 4 clips on tracks 1–4, grey like R2.

- **Where each pass lands:** one track per pass, in time order. The **first** pass is on top and is the group's header,
  the **last** pass is at the bottom.
- **Muting:** with Blend off, **every** pass is muted after Stop, the last one too. With Blend on, every pass plays.
- **Stop at a pass boundary (second session, runs R6–R9 in `fl\boundary-final.flp`, Blend off):**

  | Stop | Passes (ticks at FL's PPQ 96) | Tracks | All muted |
  |---|---|---|---|
  | within a tick of the boundary (R7) | 768, 769 | 2 | yes |
  | 3 ticks before it (R6) | 768, 765 | 2 | yes |
  | 6 ticks past it (R8) | 768, 769, 6 | 3 | yes |
  | 1 tick past it (R9) | 768, 769, 1 | 3 | yes |

  So the boundary changes nothing: the last pass is muted too, and FL keeps a pass of any length measured here, even 1
  tick (about 4.5 ms), on its own track. A stop at sample precision can't be scripted; the closest run is R7.
- **`0x2000` = muted clip, proven:** FL's Mute tool on the track 7 clip changed its flags from `0x2040` to `0x40` and its
  look from grey to bright, in the next saved `.flp`. REA_rodada2 had this as a hypothesis.
- **Recording again over the same range:** FL uses the **next free track** and starts a **new group**. It joins no earlier
  group, replaces nothing and shifts no track.
- **Files:** one WAV and one audio-clip channel per pass (`..._REC`, `_REC_2`, `_REC_3`), all routed to the same insert.
- **Group storage:** ordinary tracks. Header `grouped` 0, members 1, `expanded` (byte 61) 1, so the group is **not folded**;
  track mode 0, mixer −1. No take or comp record was found in the fields read.
- **Names:** in R0 all four tracks took the insert's name "REC". In R2–R5 the tracks kept "Track N". The `%s (Take %d)`
  string did not appear on these audio takes.
- **Loop record off (R4):** a single unmuted clip the length of the region. Whether FL kept recording past the region end
  was not determined.

- **Third session, runs R10-R13 (`E:\lynx-work\comp177c\fl2\`, Blend off, 140 BPM, loop region 2 bars). Each started from
  `takes-final.flp` with the clips, groups and first track's name and icon patched out, except where noted:**

  | Run | Setup | Clips (start tick, ticks) | Result |
  |---|---|---|---|
  | R10 | empty project, insert named REC, tracks unnamed | 408/768, 408/769, 408/219 | tracks 1-3, 2-3 grouped, **names stay "Track N"** |
  | R11 | R10's project, playhead mid-loop | 792/385, 408/768, 408/47 | **pass 1 on track 3** (free there, in R10's group), passes 2-3 on tracks 4-5, joining that group |
  | R11b | same, stopped early, 1 pass | 792/310 | track 5, the first free in that range |
  | R12 | same, stopped 131 ticks into pass 2 | 792/385, 408/131 | track 6 (header), track 7 (member): **own track though they don't overlap** |
  | R13 | track 1 kept its name "REC" and icon, no clips | 408/768, 408/769, 408/219 | tracks 1-3, **tracks 2-3 named "REC"**, no icon |

- **Rule for rows:** pass 1 goes to the first track with no clip in its range, whether named or grouped; each later pass goes
  to the next such track below the previous one, gets `grouped`, and takes the first track's name if it has one.
- **Why "REC" appeared:** the first track already had that name. Recording never takes the insert's name (R10). R0's and R1's
  track 1 showed the name, mic icon and a meter already in the first frame of recording. Which earlier action named it was not
  established; the track-link fields in the `.flp` read mode 0, mixer −1.
- **Which insert records:** the selected one. With an insert without an input selected, FL says "You didn't select an audio
  input" and aborts.
- **`.flp` export of LYNX's takes opens in FL** (`lx\rec\loopoff-export.flp`, one screenshot, no input sent): one channel shared
  by the three passes with separate sample offsets, the group and mutes as saved, row names as saved. Those takes are silent,
  so that the offsets play the right audio in FL was not heard.
- **Blend's factory default is off, from FL's own form data:** `OptionsBlendRecordedNotesAction` has no `"Checked"` in FL's DFM
  (`/mnt/e/lynx-work/r167j/dfm-all.txt` line 5164), while its neighbours `OptionsRecordingPrecountAction` and
  `OptionsTypingKbToPianoAction` have `"Checked": true`. This install's `HKCU\Software\Image-Line\FL Studio 26\ToolbarForm`
  has `BlendRecordBtn = 0` (read only, and this round toggled it earlier). A fresh settings state was not run: the registry also
  holds the licence and was left alone, and code that sets it after the form loads was not ruled out.
- Also read there: `FruityLoopsMainForm\AutoGroupLoopRecordedTakes = 1`, the "Group loop recorded takes" setting.
- FL settings changed in this session: none on purpose. The record dialog's choice ("Audio, into the playlist") was made each time.

Not observed: the state of Mixer › Disk recording › "Group loop recorded takes" (the menu did not show in my shots, so
only its effect is known), that option off, Pattern mode, Edison.

FL settings changed in these sessions: Loop record on, Metronome off, Recording precount off, Blend recorded notes off
(on in the first session, back off in the second). An accidental click opened "Remote control settings" for Master Volume; it was closed with X, with nothing
linked as far as the shots show.

### 2.2 Strings [STR] and manual [DOC]

`FLEngine_x64.dll`: `Group loop recorded takes`, `Ignore silent takes`, `Loop recording`, `Blend recorded notes`,
`%s (Take %d)`, `midi loop recording (take %d)`, `Put each take in a separate pattern`, `Merge takes to a single
pattern`. No string for a comp lane, a swipe tool, take audition or flatten was found by this keyword search, and FL's
manual describes none (recording_audio.htm, §4).

### 2.3 Inferred, not observed

- FL has no comping: inferred from the absence above and from users asking for it, not from an exhaustive REA.
- FL's project import in LYNX: the group flags import (Flp.cpp). A real `.flp` of takes has not yet been checked on screen
  in LYNX.

### 2.4 LYNX bug found: FL's clip mute was lost

`FlpImport` never read the `0x2000` flag, so all of FL's muted takes **played at once** in LYNX, and `FlpExport` dropped
muted clips. Fixed: `PlaylistItem::muted()`, imported for pattern, audio and automation clips, and exported as `0x2040`.
Test: `comp177_fl_clip_mute_survives_import_and_export`.

### 2.5 How LYNX differs from FL

| | FL, Blend off | FL, Blend on | LYNX main (round 163) | wip 177c |
|---|---|---|---|---|
| Top track | first pass | first pass | last pass | last pass (a copy) |
| Order below it | oldest to newest | oldest to newest | newest to oldest | newest to oldest |
| What plays after Stop | **nothing** | **every pass** | last pass | last pass |
| Tracks for n passes | n | n | n | **n + 1** |
| Group after recording | open | open | folded | folded |
| Files | one per pass | one per pass | one, with offsets | one, with offsets |
| Recording again over it | next free track, new group | same | next free track, no group for a single take | **more lanes in the group; can insert tracks and shift lower tracks** |

The first three columns are all of FL's and main's behaviour that this round checked; main's own differences predate this
round.

## 3. Ableton Live 12 [STR][DOC]

Strings in Live 12's `Ableton Live 12 Trial.exe` (dump in /mnt/e/lynx-work/ableton/live-exe-strings*.txt):
`CopyFromTakeLaneToContentLane` (a swipe copies into the "content lane" = the main lane), `OnToggleAudition`
(per-lane audition), `Create>Insert Take Lane`, `Create>Create Fades`, "A Take Lane Clip is also always an
Arrangement Clip.", `ATakeLaneHeader`, `AArrangerTakeLaneClipsLayerView`. The .als format (ALS-FORMAT.md):
`TakeLanes/TakeLanes/TakeLane{Name, ClipAutomation/Events/(MidiClip|AudioClip)}` and a `TakeId` on clips.

Manual (https://www.ableton.com/en/manual/comping/):
- each pass recorded over existing clips adds a take lane; the last take is copied to the main lane;
- a time selection on a lane + Enter, or a click-drag in Draw mode, copies it to the main lane; Ctrl+Up/Down swaps in
  the next/previous take for the selection;
- the main lane's clips are independent copies; the parts each lane gives are highlighted, the rest desaturated;
  dragging a highlight's edge moves the switch;
- T / the speaker button auditions a lane (one per track);
- "Create Fades on Clip Edges" (Settings › Record) puts 4 ms crossfades between adjacent clips;
- "Insert Take Lane", "Delete All Unused Take Lanes"; no flatten (consolidate like any clip).

LYNX's model is the same as Live's: lanes hold the takes, the comp track holds copies. It differs in the details users
complain about (§4).

## 4. What users praise and dislike [WEB]

- **Logic's Quick Swipe Comping is rated best.** You swipe across a take with the normal Pointer, and the same range
  is deselected on the other takes. The comp shows switch lines, and a folder can hold several named comps. Flatten
  keeps the comp as regions; Flatten and Merge writes one file. Sources:
  https://support.apple.com/sl-si/guide/logicpro/lgcp317d758e/mac,
  https://help.apple.com/logicpro/mac/10.1/en.lproj/lgcpb193382e.html,
  https://support.apple.com/en-mz/guide/logicpro/lgcpb1998a98/mac. On KVR, users say it "works a treat"
  (https://www.kvraudio.com/forum/viewtopic.php?p=8773776). Its comp crossfade length is a setting ("Crossfades for
  Merge and Take Comping": https://logicstudiotraining.com/comp-vocals-logic-pro/).
- **Live 11/12 complaints:**
  - Automation is not part of a take and overwrites the main lane. A hand-edited main-lane clip is lost when you
    record over it (SOS: https://www.soundonsound.com/reviews/ableton-live-11).
  - The lanes are "a gigantic mess" and always on. Unused takes can't be batch-cleaned
    (https://forum.ableton.com/viewtopic.php?p=1823270).
  - Edits click unless "Create Fades on Clip Edges" is on
    (https://puremix.com/blog/ill-factor-using-track-lanes-in-ableton-live-11). Users ask for configurable
    crossfades (https://forum.ableton.com/viewtopic.php?p=452244).
- **Cakewalk** is "by far the most flexible" (slip-editing and flatten). **Cubase** comps per track version (KVR,
  same thread).
- **FL users** ask for "Take Lane" with "Swipe-to-Comp" and one-click flatten
  (https://forum.image-line.com/viewtopic.php?p=2038889), and for takes nested and hidden inside their track
  (https://forum.image-line.com/viewtopic.php?p=2045874).
- **Crossfade curve and length:**
  - Equal power suits edits between different, non-coherent material (two takes). Equal gain suits coherent material
    (SOS: https://www.soundonsound.com/techniques/using-fades-crossfades).
  - Cubase's crossfades default to equal gain unless the user changes it
    (https://forums.steinberg.net/t/make-crossfade-default-to-equal-power/785131).
  - Live uses 4 ms. A few ms to about 10 ms is common practice.

## 5. "Is this the best way?" — decisions

| Piece | FL | Live 12 | Users | LYNX verdict |
|---|---|---|---|---|
| Where takes live | grouped Playlist tracks, first pass on top, open [OBS] | take lanes inside the track | FL users want takes nested and folded | **FL's grouped tracks, FL's layout** (user decision, §8); folding stays one click |
| Comp model | — | main lane holds copies; sources derived and highlighted | Logic: multiple comps per folder | **copies on the comp track; the source is derived** (same channel and audio-to-song alignment). It costs nothing and saves, exports and imports as plain clips. Several comps per group are not done (§7) |
| Choosing a part | — | selection + Enter, or Draw-mode drag | Logic's swipe with the normal tool is the favourite | **swipe on a lane** (UI slice 177c-2), plus the old "Promote" menu, now a swipe |
| Crossfade at a switch | — | 4 ms on clip edges, optional (clicks without it) | clicks are the top complaint; length should be adjustable | **always on, equal power, centred on the switch, 5 ms default, 0–100 ms per project** (`Song::compCrossfadeMs`; 0 = hard cut). It is shortened only where a take has no audio past the switch |
| The last take | its own track, muted or playing per Blend [OBS] | copied to the main lane, and has a lane | — | Its own lane, plus a copy on the comp after "Comp these takes" (§8) |
| Recording again over a comp | next free track, new group [OBS] (the 20.1 notes said sub-tracks of the parent; not what 2026 did here) | new lanes in the same track | — | **As FL: a new group on the next free tracks** (user decision, §8); the comp stays as it is |
| Audition a lane | — | T / speaker button, one lane per track | — | **`Song::auditionLane`**: that lane plays (its muted takes too) instead of the comp. Not saved, no undo entry |
| Flatten | — | none (consolidate) | Logic: Flatten, and Flatten and Merge; FL users ask for one-click flatten | **Flatten**: the comp stays, the lanes go. Merge to one file = the existing "Consolidate this track" (QuickRender) |
| Insert take lane | — | Create › Insert Take Lane | — | **Not built**: a take never joins a group now (§8), so a lane made this way would stay alone. Written and removed in this round |
| Automation in takes | — | not captured (complaint) | — | not done; automation clips are separate tracks in FL's model, so a comp never overwrites them |

## 6. Slice 177c-1: the core [CODE][TEST]

`src/core/Comping.h/.cpp` (namespace `lynx::comp`):
- `groupOf` finds the comp track and lanes [first, last] (the grouped tracks right under the comp track).
- `swipe(lane, from, to)` normalises the comp, cuts the comp clips at the range edges and copies the lane's clips in
  the range. Parts of the range where the lane has no take become silence. It then applies the crossfades. A swipe
  that changes nothing leaves the song untouched (no undo entry).
- `applyCrossfades(comp, ticks)` works at every switch point, that is, where two audio clips on the comp track touch:
  - Each side moves `ticks / 2` past the switch. The pieces overlap and get `Clip::fadeCurve` (bit 0 = equal-power
    fade-in, bit 1 = equal-power fade-out).
  - The crossfade is shortened where a take has no audio past the switch, or where a piece is shorter than the
    crossfade.
  - With no audio at all past the switch, it falls back to round 163's short linear fades.
  - `removeCrossfades` undoes this, so another length can be applied again.
- `usedRanges` tells which part of each lane the comp plays, for the lane highlight in slice 2.
- `compTakes` ("Comp these takes", §8), `setAudition` and `flatten` are described in §5.
- `takes::promoteTake` is now a swipe of that clip.
- `takes::placeTakeClips` lands loop passes as FL does, with round 163's result as an option (§8). It never touches an
  existing group.
- `PlaylistTrack::compTrack` marks a comp, saved as `playlistCompTracks` only when one exists.

Render:
- `Clip::fadeCurve` reaches the voice as `NoteEvent::fadeShape` bits 3 and 4. `clipFadeGainOf` uses
  `clipops::equalPowerCurve` (sin(x·π/2); in² + out² = 1).
- FL's automatic crossfades (`updateCrossfades`) leave comp crossfades alone.

Cost when unused: one `int` compare per clip per block in `scheduleSong` (`auditionLane >= 0`); nothing else runs
until a comp is edited.

Saving:
- In .lynx, `fadeCurve` is clip field 17 and is written only when set.
- `compCrossfadeMs` is written only when it isn't 5.
- Online Sessions sync the length with the other globals.
- .flp export writes the comp clips as ordinary clips with linear fades, since FL has no equal-power clip fade.

Tests (tests/Comp177Tests.inc, Rec163Tests.inc updated):

| Test | What it checks |
|---|---|
| `comp177_groups_and_loop_takes` | groups, every pass on a lane, last pass on the comp |
| `comp177_swipe_crossfades_and_back` | three pieces, 10-tick crossfades centred on each switch, sample-exact alignment; same swipe = no change; a swipe across a switch; re-apply with 0 / 40 ticks; back to the original comp; invalid calls |
| `comp177_crossfade_limits_and_gaps` | short-piece limit, lane gap → silence, FL's automatic crossfades don't override |
| `comp177_equal_power_render` | rendered levels inside a 100 ms comp crossfade follow sin / cos to < 0.01, and in² + out² = 1 ± 0.02 |
| `comp177_audition_flatten_and_more_takes` | scheduler: the audition plays the lane, not the comp; flatten keeps the comp, removes the lanes and moves lower tracks up |
| `comp177_fl_loop_take_layout` | §2.1's results: rows, order, mute by Blend, new group on re-record, no track moved, a tiny pass kept, a lone pass |
| `comp177_comp_these_takes` | one comp track above the group, lanes muted and named, last pass on the comp, FL's one-channel-per-pass groups |
| `comp177_existing_projects_do_not_change` | a round 163 group keeps Promote; comping it keeps every pass |
| `comp177_fl_clip_mute_survives_import_and_export` | FL's flag 0x2000 through import and export |
| `--selftest comp_crossfades` | .lynx round trip of `fadeCurve`, `compCrossfadeMs` and the comp mark; a plain project has none of them |

## 8. Decision: what a loop recording leaves behind (user, 2026-11-06)

**Decided: FL-exact recording by default, round 163's result as an option that is off, comping as an explicit step.**

- Default (`takes::placeTakeClips`): one track per pass on the next free tracks, first pass on top as the header of a new
  open group. Blend off mutes every pass, the last too, also a lone pass of a loop recording (R1). Blend on: every pass
  plays. Recording again makes a new group; no track moves, nothing is inserted or replaced. A pass of any length is kept.
  Rows follow FL's rule in §2.1 (R10-R13), with one LYNX addition: a comp group and a track linked to an insert or a channel are
  never used. How FL treats a linked track was not tested.
- Option, record menu: **"After loop recording, only the last pass plays"** (`RecordOptions::lastLoopPassPlays`, saved in
  the UI settings, off): exactly round 163's layout, including dropping a final pass up to a 1/16 note long.
- **"Comp these takes"** (Playlist track menu on a group, `comp::compTakes`, one undo): inserts one comp track above the
  group, the only time tracks move. The group's tracks become lanes (audio muted, unfolded, "<name> (take N)"; <name> is the header's, else "Track N"), and the comp
  plays the last pass. A partial last pass gives a comp only that long; the rest is swiped in.

Still different from FL, on purpose or left as shipped:

| | FL [OBS] | LYNX | Why |
|---|---|---|---|
| Files | one per pass | one file, clips with offsets | Round 163's crash-safe take stream; not part of this decision |
| Track link | none | none for loop passes; a single take still links its track (round 146a/163) | Single takes were not part of the decision |
| Single take over a linked track | next free track (R4) | the linked track when it is free there | As above |

**Best way?** Two things remain worth the user's attention:

- Blend's default: settled in §9. New installs start off, as in FL.
- A 1-tick pass gets its own track, as in FL. It is exact and a little clumsy.

### Migration: existing projects do not change

Nothing runs on load and no saved field changes meaning:

- A project saved before this round has no `playlistCompTracks`; its tracks, groups, mutes and clips load as they were.
- Its round 163 groups (header plays, everything below muted) keep "Promote this take to the comp track", recognised by that
  shape (`takes::compTrackOf`). A group with that shape made by hand, such as an FL group whose first pass was unmuted, also
  offers Promote, which replaces the header there (one undo).
- "Comp these takes" on such a group is the only way it changes: the header's last pass becomes a lane too, so it is no
  longer lost after a promote.
- `playlistCompTracks` is written only when a comp exists. An older LYNX ignores it and sees an ordinary group. FLP export
  has no such mark either: FL sees an ordinary group whose header plays and whose lanes are muted.
- Only new recordings differ: with Blend off, the last pass no longer plays after Stop unless the option is on.

**Test evidence:** `comp177_fl_loop_take_layout`, `comp177_comp_these_takes`, `comp177_existing_projects_do_not_change`, the
round 163 test run through the option, and `--selftest` (`comp_crossfades`: the mark and a plain project through save and
load).

### Run evidence [RUN]

These ran before the row rule in §2.1 R10-R13 went in; see the end of this section for the runs on the final build. Real recordings through `AudioEngine` on Windows: `LYNX --rectest <report> 18 --loop --type ASIO --device "FL Studio ASIO"` at
120 BPM, 3 passes over bars 2-4. Reports and projects are in `E:\lynx-work\comp177c\lx\rec\`.

| Run | Tracks (0 = the test's click track) | Muted | Group |
|---|---|---|---|
| Blend off | pass 1, 2, 3 on tracks 1, 2, 3 | all 3 | header 1, open |
| Blend on | pass 1, 2, 3 on tracks 1, 2, 3 | none | header 1, open |
| Blend off + "only the last pass plays" | pass 3, 2, 1 on tracks 1, 2, 3 | passes 1 and 2 | header 1, folded |

These runs prove where passes land. Each report says `result=FAILED` because there was no cable: the input stayed silent
(ping `no signal`), so no click was checked and placement accuracy was not measured here (round 163c measured it).

Shots: `LYNX --comp177shot <prefix> <project>` draws them offscreen, in `E:\lynx-work\comp177c\lx\shots\`.

**Final build, `44d664b93` [RUN].** Recording runs in `lx\rec2\`: Blend off, tracks 1, 2, 3 all muted; Blend on, none muted; Blend
off with the setting, passes 3, 2, 1 with passes 1-2 muted. Rows stay "Track N" in the first two. `result=FAILED` in each report
only means no cable was connected (ping `no signal`, 0 clicks checked), so it is not a real pass or fail of placement.

Guarded GUI session on that build, real clicks, shots in `lx\shots\` (`GUI-177c-sequence.png`, `g02`-`g09`):
right-click on a take row showed "Comp these takes"; clicking it gave "Insert 6 comp" and "Insert 6 (take 1-3)"; one click on the
toolbar's undo restored "Track 2-4" with the three muted clips; clicking the setting wrote `recordLastPassPlays=1` and showed the
tick; the tick was still there after closing and restarting LYNX; clicking it again wrote `0`.

Seen in that session: undo labels the step "Undone: Add clips". It should say "Comp these takes" (`describeChange`).

**Muted clip look:** FL marks a muted clip with an X on its header, LYNX with a veil and a diagonal across the clip. The diagonal
dates from `5833ba6fa` (2026-10-14, "Playlist: tools, clip edges, fade fix and time selection"). Round 165q only records it as
"the muted look"; no search of the docs here found a decision to differ from FL. It applies to every muted clip, so it is left for
a separate slice.

**What this slice gives users:** comping today means "Comp these takes" plus "Promote this take" on a lane clip. Swipe, audition
and Flatten are in the core and have no UI until slice 177c-2.

## 7. Not done / next

- **Slice 177c-2 (UI)**, after the coordinator's FL | Ableton | LYNX shot review:
  - a swipe on a lane (the lane's own rows, no extra tool) and the used-range highlight on the lanes;
  - an audition button on each lane header;
  - the comp-crossfade length in Project settings;
  - "Flatten comp" in the track menu;
  - the equal-power curve drawn on comp crossfades.
- Several comps per group (Logic's comp list) and Ctrl+Up/Down to cycle takes for a selection (Live) are possible
  later additions on the same model.
- Open for the UI slice: a swipe must not act on an ordinary track group (no take clips); the audition must clear when
  tracks are inserted or removed and before an export, because `scheduleSong` follows it there too.
- A second recording over a comp is a separate group, so its passes can't be swiped into the first comp. A later explicit
  "add these takes to the comp" is possible; not built, and not asked for.
- Live .als take-lane import: `core/AlsImport` landed (round 175a); whether it maps `TakeLanes` to lanes is not part of this
  round (ALS-FORMAT.md documents them).

## 9. Blend recorded notes starts off (slice 177c-blend)

**FL:** off by factory form data (§2.1): the DFM's `OptionsBlendRecordedNotesAction` has no `"Checked"`, while its neighbours have
`"Checked": true`. This is static evidence. **FL was not run from a fresh state**, because that would need a registry edit, and
registry edits are not allowed here. Code that sets the switch after the form loads was not ruled out.

**LYNX before:** on (`RecordMode::overdub`), and the switch was not saved at all: every session started on, whatever was
chosen last time.

**Now:**

| Who | Starts as | Why |
|---|---|---|
| New install (no `ui-options.txt`) | **off** | FL's default |
| Existing install, settings file without `recordBlend` | **on** | That is what every earlier session started with, so nothing changes for them |
| After any save | what was last chosen (`recordBlend=0/1`) | The switch is remembered now, as FL does (`ToolbarForm\BlendRecordBtn`) |

**Migration note:** an existing user sees no change until they flip the switch; a new user gets FL's behaviour: Blend off
means a loop recording's passes are all muted after Stop, and a new note take replaces the recorded channel's old notes. Nothing
in a project changes. Blend isn't a project setting in LYNX or in FL.

**Test evidence:** `comp177_blend_starts_off_only_for_new_installs`. Command-line record runners (`--rectest`,
`--countinrectest`, `--bendrectest`) now set their mode explicitly, so their reports keep meaning what they did.

## 10. Muted clips look as in FL (slice 177c-mute)

**FL [OBS]** (`fl\f00.png`, `takes-final.flp`): an X sits where the clip's type glyph is, in the title, even on a clip too narrow
for the name. The whole clip is dimmed, title included, and the waveform stays readable.

**LYNX before:** a veil plus a diagonal line across the clip (`5833ba6fa`, 2026-10-14), which reads like an automation line.

**Now:** no diagonal. The veil stays and is a little lighter (0.62 to 0.55) and the X takes the glyph's place on audio, pattern and
automation clips from 14 px wide, drawn over the veil so that it stays readable, in the theme's secondary text colour: a little above the dimmed name, as FL's is (FL's X peaks at about (127, 141, 151), its muted name at about (112, 131, 141)). It is LYNX's own drawing of
a plain X: no FL artwork is used. Painting cost for unmuted clips is unchanged.

The 14 px cutoff has **no FL evidence below about 36 px**, FL's narrowest clip in these shots: narrower LYNX clips show no X and rely on the dimming. The neutral grey of the muted waveform is the theme's and stays (FL's is blue-grey).
## 11. Comping UI (slice 177c-2)

FL has no comp UI (§2), so this follows Ableton's lanes and Logic's Quick Swipe, the favourite in §4.

| Piece | Ableton Live | Logic | LYNX |
|---|---|---|---|
| Swipe | Draw mode, or selection + Enter | Normal Pointer | Normal tool. A drag on a lane clip's **body** swipes; its title still moves it and its edges still resize |
| Used parts | Highlighted, the rest desaturated | Highlighted | The used part stays undimmed with an accent bar along its foot; the rest stays dimmed with the X |
| Audition | T or speaker button, one lane | — | Speaker button on the lane header, one lane, no undo entry, not saved |
| Crossfade | 4 ms in Settings › Record | Per comp | 5 ms by default, Project settings › Comp crossfade, 0 to 100 ms, re-applied to every comp in one undo step |
| Flatten | None (consolidate) | Flatten | Track menu, "Flatten comp" |

**Best way?** Swiping with the normal tool costs nothing to learn, but it takes the body drag away from moving a lane clip. The
title keeps that, and only lanes of a comp track change. An ordinary group is not touched: `comp::isComp` must hold.

Guards from §7:
- The audition ends on any edit that moves or renames the auditioned row (`comp::checkAudition` in `AudioEngine::editSong`),
  on undo and redo, and is ignored by a render (`exportopts::makeRenderSong`). It costs one compare when unused.
- The used ranges are worked out once per paint, and only if a comp track exists.
- Undo names: "Comp takes", "Swipe to comp", "Flatten comp".

**Guarded GUI session [RUN]** (Windows build of `ffe735c99`, shots in `E:\lynx-work\comp177c\lx\ui\g-*.png`):
- Blend: a fresh profile starts **off**; a profile with a pre-0.1.25 `ui-options.txt` (no `recordBlend`) starts **on**; one
  click turns it off and writes `recordBlend=0`; after a restart it is still off.
- Muted clips in the live window: X in the title, clip dimmed (`g-1`).
- Track menu › Comp these takes; a drag across take 1's body shows the range while dragging and swipes it on release
  (`g-4pair`): the comp gets three pieces, take 1's used part and take 3's two parts are lit.
- The speaker on take 2 lights and the hint bar says what it does (`g-5c`). Flatten comp from the comp track's menu leaves
  the comp's three clips and removes the lanes (`g-78`).
- Undo (Ctrl+Alt+Z) reads "Undone: Flatten comp", then "Add clips" for the swipe (now "Swipe to comp", fixed after this
  session; test `comp177_undo_labels_for_the_comp_steps`), then "Comp takes". The undo also ended the audition.
- Project settings shows "Comp crossfade 5.0 ms" (`g-14`). Changing it was not tried in the GUI.
- Not checked here: the audition by ear, and the crossfade curve in the live window (offscreen shot `u-crossfade.png`).

## 12. `.flp` offsets with a real signal [RUN]

The take WAV of `lx\rec2\off-project.lynx` replaced by seeded noise (`lx\offs\gen.py`; every range unique), four projects:
each pass unmuted alone, and a comp of three one-bar pieces from passes 1, 3, 2. LYNX renders them (`--export`); FL renders
LYNX's `.flp` exports with its own command line (`FL64.exe /R /Ewav`, no input, no registry edit). `cmp.py` finds where each
render's audio comes from in the source; `d.py` compares the FL and LYNX renders.

| Project | Source position FL plays (expected) | FL vs LYNX |
|---|---|---|
| pass 1 | 173 406 (173 400 + 6) | lag 0, correlation 0.99990, gain 0.9998 |
| pass 2 | 461 406 (461 400 + 6) | lag 0, correlation 0.99929, gain 0.9998 |
| pass 3 | 723 981 (723 975 + 6) | lag 0, correlation 0.99991, gain 0.9999 |
| comp, 3 pieces | 125 400 / 797 400 / 605 400, exact | lag 0, correlation 0.99966, gain 0.9986 |

The 6 samples are the passes' saved sub-tick fraction (0.24 tick), which both play; the comp pieces have none. Both start the
audio at exactly 2.000 s. FL's audio ends about 280 samples (6 ms) after LYNX's, a tail not looked into. Renders at 44.1 kHz
from a 48 kHz file, so the residual against the source (−16.5 dB) is the resampling, not an offset.

**Coordinator review of 177c-2 (2026-11-07):**
- Comp row names: "Track N comp" (N = the group header's row number, which the comp takes over), lanes "Track N (take K)",
  in line with the take rows' "Track N". A header the user named gives "<name> comp" / "<name> (take K)". The insert's mixer
  name is no longer used (the GUI session above still showed "Insert 6 comp"). Test: `comp177_comp_these_takes`.
- **Open:** FL's render ends about 6 ms (280 samples at 44.1 kHz) after LYNX's on the same `.flp` (§12). Not looked into.

## 13. How Live swipes, and the swipe choice [RUN]

Live 12.4.16 Trial, a copy of Live's own Patience demo with "Vocal Main" given three audio take lanes and a comp in progress
(`E:\lynx-work\comp177c\live\`): a plain drag on a lane body only makes a time selection (main lane unchanged); Enter then
copies it to the main lane; in Draw mode (B) a drag on a lane copies at once. Used parts are lit in the take's colour.
LYNX swipes on a plain drag (Logic's Quick Swipe). The choice is the user's: (a) keep, (b) Live's way, (c) both.

Prepared behind `PlaylistComponent::swipeStyle` (default (a), so nothing changes until the decision):
- (b) `liveSelectEnter`: only the brush tool (LYNX's nearest to Live's Draw mode; LYNX's default pencil is FL's) swipes on a
  drag; "Send the time selection to the comp" (lane track menu, action `playlist.swipeSelectionToComp`) does Live's Enter.
- (c) `both`: the drag swipes with any tool, and the command works too.
- No key is bound: Enter is FL's "Maximize or restore the Playlist" (`panel.maximisePlaylist`). Giving Enter to the comp
  when a lane has a time selection would change an FL key, which needs the user's word.

Live session notes: Live's plug-in scan raised Antares PACE "Subscription License Has Expired" boxes (3 at start, 16 on the
restored scan), each closed with Quit only. Live's VST3 system folders were turned off for the session in Live's Settings and
turned back on the same way; `Preferences.cfg` then differed from the backup in the recent-files list and browser rows.

**User decision (2026-11-07): (c), both.** `comp::swipeStyle = both` (core/Comping.h): a drag on a lane clip's body swipes with
the pencil (plain drag) or the brush (Draw mode), and "Send the time selection to the comp" (lane track menu; shortcut editor
`playlist.swipeSelectionToComp`, no default key) sends a time selection, as Live's Enter does. Enter stays FL's Maximize
Playlist. Test: `comp177_three_ways_to_swipe`.

## 14. Living with round 180d's FL fades (merge of 2026-11-08)

Round 180d (FL's fade tension, straight fades, FL's fade flags) landed first and owns the clip array's indices 17-20. The comp
curve (`Clip::fadeCurve`) is saved at index 21, written only when it is not 0; no released LYNX wrote it at 17. The voice keeps
180d's rules (FL's S curve by default, `(i+1)/(N+1)`, tension) and the comp crossfade's equal-power curve (shape bits 3 / 4)
wins over them, without tension. FLP export is 180d's: a comp crossfade goes to FL as an ordinary fade of the same length (FL
has no equal-power curve). Selftest `comp_crossfades` saves and loads one clip with every one of these fields.
