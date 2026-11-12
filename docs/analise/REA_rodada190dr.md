# Round 190dr: Live screens for the Clip-launch workflow (0.1.27)

Items #23, #37 and #26 of `/mnt/e/lynx-work/rea190bk/WHAT-LYNX-LACKS.md`. The Pattern workflow is untouched: everything
below lives in the clip grid and the Detail View, which exist only in the Clip-launch workflow. Facts are in our own
words; no Live code, art or text is in the repository.

Evidence used:
- `[S1]` `/mnt/e/lynx-work/ableton/probe/p3/session1.png` — Live 12.4.16 Session View with its mixer (13 audio
  tracks, no returns): per strip "Audio From / Ext. In / 1", "Monitor In Auto Off", "Audio To / Main", an empty sub
  box; the Volume section with the volume box (-3.00), the pan knob, the Track Activator (orange, the track number),
  S, the arm button, the meter with a triangle handle and the labels 0 12 24 36 48 60; the Main strip under the
  scenes ("Cue Out", "Main Out", volume 10.1, pan, Solo, the cue knob); the Info View text "Drag the triangular
  handle to adjust the volume smoothly. Double-click the meter to reset the volume". Zooms:
  `/mnt/e/lynx-work/livescr190dr/ev/live-session-mixer-*.png`.
- `[STR]` `/mnt/e/lynx-work/ableton/live-exe-strings.txt`: `View>Mixer`, `View>Mixer Controls>In/Out | Sends |
  Return Tracks | Volume | Track Options | Crossfader | Performance Impact`, `sInfoTextShowMixerConfigMenu`,
  `OnStereoSplitPanContextMenu`, `OnMonitorContextMenu`; u16 table: `Track Delay`, `Mixer.CrossfadeAssign`,
  `Set Clip Start`, `Set Clip End`, `Warping`, `Warp Mode`, `Clip Gain`, `High Quality Rate Conversion`, `Clip RAM Mode`.
- `[XML]` `/mnt/e/lynx-work/ableton/xml/DefaultLiveSet.xml`: `MixerInSession 1`, `SessionIO 1`, `SessionSends 1`,
  `SessionReturns 1`, `SessionVolume 1`, `SessionTrackOptions 0`, `SessionCrossFade 0`,
  `ViewStateSessionMixerVolumeSectionHeight 120`.
- `[S2]` `/mnt/e/lynx-work/warp190cg/shots/live-clipview-audio-panel.png` — Live 12 Clip View of an audio clip: Clip
  panel "Start [Set] End [Set]", Loop, "Position [Set] Length [Set]", Signature, Groove, Scale; Audio panel
  "[Warp] [Follow]", mode chooser, BPM /2 x2, Gain 24 … 70, Pitch knob with st / ct boxes, Reverse, Edit, Fade, RAM, HiQ;
  the sample editor's title "loop16.wav 48.0 kHz · 16-Bit · 2 Ch". The Set buttons are dim (the clip is not playing).
- `[MAN]` Live manual text as quoted in the coverage rows (`rea190bk/out/live4.json` deviceview-0012…0016; Track
  Delay sign; Exclusive Solo).

## 1. Session mixer (#23) — `core/SessionMixer.h`, `ui/SessionMixer.{h,cpp}`

A grid column is a Playlist track; its strip is the mixer insert the track plays through, by the Device View's rule
(`detailchain::forTrack`), so the Mixer, the Device View and the strip are one insert.

| Item | LYNX | Evidence | Exact / approximate |
|---|---|---|---|
| Mixer under the slots, below the status row; Main strip under the scene column | `ClipGridComponent` places `SessionMixerComponent` | [S1] | exact placement |
| Sections In/Out, Sends, Track Options, Volume; show / hide each and the whole mixer | Mixer button under the grid + grid menu › Mixer; saved in `.lynx` (`ClipGrid::mixerView`, `mixerView` array, only when not default) | [STR] [XML] (defaults: all on but Track Options) | approximate: Live has them in the View menu; LYNX's grid has no View menu of its own (the Grooves button is the same pattern) |
| Volume box in dB with two decimals, -inf | `volumeText` | [S1] "-3.00" | exact text; LYNX fader top +5.6 dB (Live +6) |
| Meter with a triangle handle on its left, labels 0…60 | drawn on LYNX's meter scale (-60…+6 dB) | [S1] | layout exact, scale LYNX's |
| Drag the handle; double-click the meter = 0 dB | `Part::fader` | [S1] Info View text | exact |
| Pan knob under the volume box | `Part::pan` | [S1] | exact; no Split Stereo pan mode (row session-062) |
| Track Activator with the track's number | the insert's mute | [S1] | exact; LYNX's green LED colour, not Live's orange |
| Solo (S): exclusive, Ctrl adds | `sessionmixer::solo` | [MAN] Exclusive Solo default | exact; Solo in Place is a returns option, LYNX has no returns |
| Arm button in the Volume section | the grid arm of round 190bv (moved out of the status row while Volume shows) | [S1] | exact place; round 190cy's noted deviation is gone |
| Audio From: Ext. In / No Input, then the channel (1, 1/2) | the insert's input (`setInsertInput`) | [S1] | exact for audio tracks |
| MIDI From on instrument tracks | a label "All Ins" (LYNX's armed / selected instrument takes every MIDI input) | [S1] (MIDI tracks: Live's manual) | approximate: no chooser, no channel box, no Monitor |
| Monitor In / Auto / Off | the insert's monitor; LYNX's extra "When armed" lights Auto | [S1] | exact for the three |
| Audio To: Main or another track | `sessionmixer::setOutput` (a unity route first in the list, the Master route off) | [S1] | approximate: no sub-box (Live's is empty for Main), no Ext. Out / Sends Only choice in the menu (shown when an insert has only sends) |
| Sends: a knob per row | rows = every insert a strip's insert sends to (max 6); turning a knob up from zero makes the send | [XML] SessionSends; [STR] | approximate: Live's rows are its return tracks; any LYNX insert takes sends. Knob + target name layout: **needs a live check** |
| Track Options: Track Delay (ms) | the insert's manual latency with the sign turned (Live: positive = later) | [STR] `Track Delay`; [MAN] | box layout: **needs a live check** |
| Meters | a third peak reader in `MeterBank` (`takeSessionPeak`), so the Mixer's and the Playlist headers' meters lose nothing | — | — |
| Fit | sections that do not fit leave from the top (In/Out, Sends, Track Options), the slots keep two rows | — | LYNX's choice |

Left out (no dead controls): Crossfader section and A/B assign (needs an engine path; LYNX has no crossfader), crossfader
curve, Performance Impact, Return tracks section, Cue Out / Main Out / Solo-Cue switch / cue volume on the Main strip,
Split Stereo pan, track width drag, MIDI input chooser.

## 2. Clip View Sample panel (#37) — `core/ClipSample.h`, `ui/ClipAudioPanel.cpp`, `ui/DetailView.cpp`

Already there (round 190cg): mode chooser, Gain, Pitch st / ct, Reverse, the docked audio editor with warp markers and
the segment BPM with /2 x2; Start / End / Loop / Length / Signature / Scale in the Clip panel (round 183a).

| Item | LYNX | Evidence | Exact / approximate |
|---|---|---|---|
| Warp switch above the mode chooser | on = the channel's `stretchBeats` > 0 (the sample follows the tempo), off = 0; the warp markers stay; every clip of the channel keeps playing the same part of the file (offset / length scale) | [S2] | the switch is exact; LYNX's warp belongs to the channel (decision K), Live's to the clip. Warp on without markers takes the file's length at the song tempo: **needs a live check** (Live may guess a tempo) |
| Sample title "name  rate · depth · channels" over the editor | `clipsample::infoText`, read from the file's header | [S2] | exact text; LYNX's own small mark instead of Live's icon |
| Set beside Start and End | puts the marker where the playing clip is (`playPosition` from the launcher's progress); dim while it does not play | [S2] dim Set buttons; [STR] `Set Clip Start` | exact for Start / End, for MIDI and audio clips |

Left out in the first pass and built in 2b below: Edit, Fade, Position / Length with Set, Groove chooser, BPM in the
panel. Still left out: Follow (meaning not known: **needs a live check**), RAM, HiQ, a Groove chooser for audio clips.

### 2b. After the coordinator's review (190dr-2)

The Clip and Audio sections now follow [S2] field by field:
- Clip: Start [Set] | End [Set], Loop, Position [Set] | Length [Set] in "bar. beat. sixteenth" (lengths from 0),
  seconds "m:ss:mmm" for unwarped audio; Signature (the project's) | Groove (the Groove Pool's grooves, MIDI clips only);
  Scale. LYNX's loop starts at the clip start, so Position moves Start and End together (approximate; a separate loop
  start needs launcher work).
- Audio: Warp, the mode list with Live's names on LYNX's engines (Re-Pitch = resampling; Complex Pro left out), BPM with
  ÷2 / ×2 (warp190cg's command), Gain with its dB read-out, Pitch knob with st / ct, Reverse | Edit (LYNX's audio editor
  window), Fade (the clip's edge fades, 4 ms: LYNX's length). Follow, RAM and HiQ left out (no matching behaviour).

## 3. Device View: a device to another track (#26) — `core/DetailChain.h` `moveEffectToInsert`

Already there (round 190ce): Cut / Copy / Paste / Duplicate of effect cards, Ctrl-drag copy inside the chain.

| Item | LYNX | Evidence | Exact / approximate |
|---|---|---|---|
| Drag a device to another track: it moves | drag a card's title bar out of the Device View onto a grid column (header, slots, status row or mixer strip; the column is outlined) | [MAN] deviceview-0015 | the gesture is exact; it lands at the end of the target chain (**needs a live check**) |
| Ctrl at the drop: a copy | a new device (`pasteEffect`), the original stays | [MAN] deviceview-0014 | exact |
| What goes along | a move keeps the device (its running instance and settings) and its automation, MIDI links, modulator routes and controller sources; a side input that would be its own insert is dropped | — | LYNX's choice (**needs a live check** for automation) |
| Back into its own chain | a move or Ctrl copy at the drop place, as a plain drag | — | — |
| Undo | one step (`editSongStructural`); checked in `--selftest` (`livescr190dr=OK`) and `--liveshot190dr` | — | — |

Left out: drop onto the Playlist's track headers (the Arrangement), drop on the empty area to make a new track
(deviceview-0016), several devices at once, instrument cards (a LYNX instrument is a channel).

## Best way?

- **Session mixer.** FL: the Mixer is a window of its own with every insert; the Playlist track has no strip. Live:
  a strip under each Session track [S1]. Users praise Live's "mix where you jam" and complain the Session mixer eats
  slot room on laptops. Verdict: Live's strips in the Clip-launch workflow only, every section switchable and saved per
  project (as Live), sections leave from the top when room is short. Nothing changes in the Pattern workflow.
- **Sends rows.** Live has return tracks; LYNX (FL) sends to any insert. Showing the send targets as rows keeps one
  model; a "returns only" option could come later if users ask.
- **Warp per channel.** Live warps per clip; LYNX's stretch is per channel (decision K). The switch scales every clip
  of the channel so none of them jumps to another part of the file; the tooltip says it applies to the channel.
- **Device drag.** FL moves a slot to another insert only through the Mixer's slot menu / Alt-drag; Live drags devices
  to tracks. Verdict: the drag, with automation following (nothing goes silent unnoticed).

## Needs a live check (Live 12.4.16)

1. Sends section: a knob per return, its letter and where it sits; Track Options' Track Delay box; the teal bar in each
   status cell of [S1] (Performance Impact or clip position). REA unknown `unk_ee4663be…`.
2. A device dragged onto another track: place in the chain, automation, selection afterwards; Ctrl copy.
   REA unknown `unk_700cf30a…`.
3. Warp on for an unwarped clip without markers; Follow; when Fade / RAM / HiQ are enabled on Session clips.
   REA unknown `unk_90949ceb…`.
4. MIDI track In/Out (MIDI From chooser and channel, its Monitor) and Audio To's sub box.
5. The Main strip's Solo / Cue switch and cue knob (left out).

## REA toolset

`record_unknown` (REA MCP) worked for the three open questions above. No binary session was opened: the program's
string tables, the default set's XML and the shots on disk answered the rest, so the Ghidra lock was not taken.
