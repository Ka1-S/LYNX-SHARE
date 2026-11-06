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
- **Limit of that finding:** every Blend-off run was stopped partway through its last pass. A stop exactly on a pass
  boundary was not tested.
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

Not observed: the state of Mixer › Disk recording › "Group loop recorded takes" (the menu did not show in my shots, so
only its effect is known), that option off, Pattern mode, Edison.

FL settings changed in this session: Loop record on, Metronome off, Recording precount off, Blend recorded notes on
(left on). An accidental click opened "Remote control settings" for Master Volume; it was closed with X, with nothing
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
| Where takes live | grouped Playlist tracks, first pass on top, open [OBS] | take lanes inside the track | FL users want takes nested and folded | **FL's grouped tracks** (no new format). Layout and folding: pending the user's decision, §8 |
| Comp model | — | main lane holds copies; sources derived and highlighted | Logic: multiple comps per folder | **copies on the comp track; the source is derived** (same channel and audio-to-song alignment). It costs nothing and saves, exports and imports as plain clips. Several comps per group are not done (§7) |
| Choosing a part | — | selection + Enter, or Draw-mode drag | Logic's swipe with the normal tool is the favourite | **swipe on a lane** (UI slice 177c-2), plus the old "Promote" menu, now a swipe |
| Crossfade at a switch | — | 4 ms on clip edges, optional (clicks without it) | clicks are the top complaint; length should be adjustable | **always on, equal power, centred on the switch, 5 ms default, 0–100 ms per project** (`Song::compCrossfadeMs`; 0 = hard cut). It is shortened only where a take has no audio past the switch |
| The last take | its own track, muted or playing per Blend [OBS] | copied to the main lane, and has a lane | — | Coded as a lane plus a copy on the comp. Differs from FL: pending §8 |
| Recording again over a comp | next free track, new group [OBS] (the 20.1 notes said sub-tracks of the parent; not what 2026 did here) | new lanes in the same track | — | Coded as new lanes under the comp track. Differs from FL and can shift tracks: pending §8 |
| Audition a lane | — | T / speaker button, one lane per track | — | **`Song::auditionLane`**: that lane plays (its muted takes too) instead of the comp. Not saved, no undo entry |
| Flatten | — | none (consolidate) | Logic: Flatten, and Flatten and Merge; FL users ask for one-click flatten | **Flatten**: the comp stays, the lanes go. Merge to one file = the existing "Consolidate this track" (QuickRender) |
| Insert take lane | — | Create › Insert Take Lane | — | **`comp::makeLanes`**: a track's audio becomes lane "take 1", so the next take over it becomes a lane too |
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
- `setAudition`, `flatten`, `addTakeLanes` and `makeLanes` are described in §5.
- `takes::promoteTake` is now a swipe of that clip.
- `takes::placeTakeClips` changed in three ways:
  - The last pass also gets a lane.
  - Takes over a comp group of the same insert become more lanes of it, only when every clip they replace is also on a
    lane (an ordinary group keeps its clips and the take goes to a new track).
  - When the tracks under the new comp track are not empty, the lanes are inserted under it rather than spread.

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
| `comp177_audition_flatten_and_more_takes` | scheduler: the audition plays the lane, not the comp; a second loop recording adds lanes (take 4–6) and puts its last pass on the comp; flatten; makeLanes + a punch take with pre-roll becomes lane "take 2" and goes to the comp with a crossfade into the pre-roll |
| `--selftest comp_crossfades` | .lynx round trip of `fadeCurve` and `compCrossfadeMs`; a plain project has neither |

## 7. Not done / next

- **Slice 177c-2 (UI)**, after the coordinator's FL | Ableton | LYNX shot review:
  - a swipe on a lane (the lane's own rows, no extra tool) and the used-range highlight on the lanes;
  - an audition button on each lane header;
  - the comp-crossfade length in Project settings;
  - "Flatten comp" and "Insert take lane" in the track menu;
  - the equal-power curve drawn on comp crossfades.
- Several comps per group (Logic's comp list) and Ctrl+Up/Down to cycle takes for a selection (Live) are possible
  later additions on the same model.
- Open for the UI slice: a swipe must not act on an ordinary track group (no take clips); the audition must clear when
  tracks are inserted or removed and before an export, because `scheduleSong` follows it there too.
- Behaviour changes awaiting a decision: the last pass's extra lane, and lanes inserted under a comp pushing lower tracks
  down.
- Live .als take-lane import: `core/AlsImport` landed (round 175a); whether it maps `TakeLanes` to lanes is not part of this
  round (ALS-FORMAT.md documents them).
