# Round 186c: mixer Delete (FL-exact), links to deleted inserts, mixer groups that follow the Playlist

FL Studio 2026 (`FL64.exe`/`FLEngine_x64.dll`, build 5654), own headless Ghidra project of round 177a
(`~/ghproj/r177a`, read-only) plus a hand check in FL on 2026-11-09 (shots in `/mnt/e/lynx-work/mixer186c/shots`).

## 1. FL's "Delete" on mixer tracks

### Static (REA)

`FUN_012a5b60` (reached from the mixer track menu's Delete and the Del key handler `FUN_0129c980`):

1. Collects every **selected** mixer track (record flag `+0x19`) into a list.
2. While audio is being recorded (`[015e09f8] == 1` and armed inserts > 0):
   `Error|It isn't possible to delete mixer tracks during audio recording.` (flags 0x10, OK only). Nothing is deleted.
3. `FUN_00c26810 (0x32)` selects message record 0x32 of the table at `0x01500260` (0x24 bytes each:
   id `DeleteMixerTrack`, label `Delete mixer track`, category `File/Data`).
4. `Delete tracks|This will |delete| %d selected mixer track(s). Are you sure?`, shown by `FUN_009635b0` with flags
   `0x20024`: `MB_YESNO | MB_ICONQUESTION` plus FL's own `0x20000`. `FUN_00964d10` turns `0x20000` into the
   **"Remember my choice"** box (without it, `0x10000000` shows "Don't show this in the future").
5. On Yes (6), it checks whether any selected track is linked to a Playlist track (`FUN_012a5b00`: mode 1 = audio, the
   track's own mixer track; mode 3 = instrument, the channel's mixer track). If so:
   `Playlist tracks|One or more mixer tracks that will be deleted are connected to instrument/audio tracks.` +
   `Do you want to unlink the tracks and channels or delete them?`, buttons `Delete tracks and channels` (0x100),
   `Unlink tracks and channels` (0x101), `Cancel` (anything else: nothing happens). No remember box.
6. `FUN_012a5280 (list, count-1, flags)` deletes them; the undo step is "delete mixer tracks".

FL 21's change log adds: "Tracks routed to a deleted mixer track are now routed to the destinations of the deleted
track" (19603; 20677 fixed a feeding track going to the Master instead).

### Checked by hand in FL 2026

| Step | FL shows / does |
|---|---|
| Insert 6 selected, Del | "Delete tracks", "This will **delete** 1 selected mixer track(s). Are you sure?" (delete in the accent colour), "Remember my choice", Yes / No (`fl-delete-dialog.png`) |
| Remember ticked + No | nothing deleted; the next Del asks again: **No is not remembered** |
| Remember ticked + Yes | deleted; the next Del deletes at once, no question |
| Options › General settings › Hints and warnings › **Manage messages** | a checklist by category (Audio, Behavior, File/Data, Notifications, Trial) + Show all / Show none; "Delete mixer track" (File/Data) was unticked; ticking it brings the question back (`fl-manage-messages.png`) |
| Playlist track 1 set to Audio track › Insert 6, then Delete insert 6 › Yes | "Playlist tracks" with the three stacked buttons (`fl-delete-linked-dialog.png`) |
| … › Unlink tracks and channels | insert deleted, the Playlist track keeps its name ("Insert 6"), unassigned |
| … › Delete tracks and channels | insert deleted, the Playlist track is reset ("Track 1", unassigned) |
| After any delete | the selection stays on the same number (the next track); undo = "Undone: delete mixer tracks" |

### LYNX (this round)

`mixerops::deleteTracks` + `MixerStrips::deleteTracks`, `showFlMessage` (Dialogs.h) and `messages::*` (Settings ›
General › Hints and warnings › Manage messages). Matches every row above. Differences:

- LYNX keeps its insert count (a blank insert takes the last place, as before); FL's mixer shrinks.
- "Delete tracks and channels" deletes an instrument track's channel; for an audio track LYNX resets the Playlist track
  only (FL's handling of an audio track's clip channels was not checked).
- Manage messages lists only the messages LYNX has (Delete mixer track); FL lists ~50.

**Best way?**

- **FL:** the above (REA + hand check).
- **Ableton Live 12:** deleting a track never asks; Undo is the safety net (STUDY.md).
- **Users:** FL users ask for fewer pop-ups in general, and the remember box is FL's own answer to that.
- **Verdict:** FL-exact by default (the question protects a mixer with plugins); Remember my choice gives Live's flow.

## 2. Links to deleted inserts (pain point 1, LYNX option, off)

FL drops a send into a deleted insert. Users ask to see the broken link instead (research-web-2 §3.1). Mixer menu ›
"Keep links to deleted inserts, greyed (LYNX)": each send into a deleted insert stays as `MixerTrack::lostSends`
(the deleted insert's name and the send level, saved in the project, never in the .flp). The selected insert shows
them as a greyed dashed chip ("× name", or "× N") in its own route-switch place; the insert menu's Routing section ›
"Disconnected links" reconnects one to another insert at its level, or removes it. The sound is the same as with the option off.

**Best way?** FL: drops it. Live: a send to a deleted return goes too. Users: want "routing by reference" and a visible
broken link. Verdict: an option, off by default (FL-exact default).

## 3. Mixer groups that follow the Playlist (pain point 4, LYNX option, off)

FL has no link between Playlist track groups ("Group with above track", "Collapse grouped tracks") and mixer groups
(separators). Mixer menu › "Mixer groups follow the Playlist (LYNX)": the separators and folds of the inserts linked to
Playlist audio / instrument tracks mirror the Playlist's groups, after every edit (outside the undo history; the
inserts no Playlist track names keep their separators). A group whose inserts are not side by side waits for Mixer menu
› "Arrange inserts like the Playlist", which moves the linked inserts to the front in Playlist order (references
follow the inserts, round 177a) and applies the groups as one undo step.

**Best way?** FL: none. Live 12: group tracks are the same object in Arrangement and Mixer, so they always match.
Users: ask for mixer folders that mirror the Playlist (IL mixer wishlist Jun 2025). Verdict: optional, off by default,
plus the explicit Arrange action (the user's decision of 2026-11-08 07:17).
