# REA round 190bx: FL Studio's Settings options LYNX did not honour (2026-11-11)

Scope: the "Settings" area of the FL coverage inventory of round 190bc (393 rows, 220 counted; 47.3 % before this
round). Every row that was missing or partial was looked at again; this document records what FL does for the ones
LYNX now honours, and why the others were left out.

Method: static only, on the FL Studio 2026 install of this machine: the Settings form's resource (captions, hints,
positions and the design-time state of each box) and the option handlers of the engine library, read in a headless
Ghidra project. Clean-room: what follows are facts in our own words; no code, text table or resource of FL is in the
repository. FL's labels are used as labels (they are what a user looks for). Nothing was run or injected.

A design-time `Checked` on the form is FL's default: FL's loader copies the option flags to and from the form's boxes,
and a fresh install shows the boxes as designed.

## 1. What FL does

### 1.1 Audio › Mixer › "Resampling quality" [STATIC: form + handler]
- A drop-down with the hint "Interpolation method for sampler channels". Items, under three headings: "Low quality /
  fastest": 2-point linear, 6-point Hermite; "Average quality": 16-, 24-, 32-point sinc; "High quality / slowest":
  64-, 128-, 256-, 512-point sinc. Default: 24-point sinc.
- The handler stores item + 1 as the engine's interpolation index (1 … 9, the same scale as the render dialog's
  resampling list studied in round 168) and tells every loaded plugin about the change. When the chosen item is
  64-point or higher it shows a warning that the method is heavy for real-time use.
- It is an app option, not saved in a project. An export uses the render dialog's own list (default 32-point sinc).

### 1.2 Audio › CPU [STATIC: form + handlers]
- "Multithreaded generator processing" and "Multithreaded mixer processing", both ticked by default, hint "May
  increase CPU performance". Each handler stores its flag and re-applies the threaded state of every channel / every
  mixer slot (their own "Allow threaded processing" switch, round 134, still counts).
- "Smart disable", ticked by default: the handler re-applies the smart-disable state of every effect slot and every
  generator. It is the master switch over the per-plugin "Smart disable" (round 139): off means nothing is put to
  sleep.
- "Align tick lengths": an internal of FL's tick-sliced mixing; LYNX mixes whole device blocks. Out of scope.

### 1.3 Audio › Output › "Auto close" [STATIC: form + handlers + two helpers]
- Hint: "Free the audio device for other applications". Default off.
- The flag is read where the program loses and regains focus, and in the stop routine: with the option on and the
  transport stopped, losing focus closes the open device; coming back opens it again (the two helpers do exactly
  that: one closes when a device is open, the other opens when none is, then refreshes the status display).
- Not established statically: whether FL also closes a while after "stop" when it is still in front (the stop routine
  reads the flag, the branch was not followed). Needs a live check.

### 1.4 General › Miscellaneous [STATIC: form + channel solo routine]
- "Restore previous state after solo", default off, hint about restoring the previous active state of channels and
  tracks. In the channel solo routine: when a solo starts, every channel it touches (not locked, in the shown group)
  remembers whether it was on; a memory that already exists is not overwritten (moving the solo keeps the state from
  before the first solo). When the solo ends, the touched channels are all switched on if the option is off, and only
  the ones that were on if it is ticked. The option is read when the solo ends.
- "Auto name channels", default on, hint "Channel name will be set to the sample or preset filename".
- "Silent startup", default off: FL does not play its sound on startup. LYNX already has the inverse switch
  ("Play the startup sound", General).
- "Auto zip empty channels", "Auto zoom in piano roll", "Auto name effect slots", "Alternate meter scale", "Small
  scrollbars in editors", "Hide plugin window toolbar by default": handlers not studied this round (see 3).

### 1.5 General › Advanced › "Read sample tempo information" [STATIC: form + sample loader]
- Default on. When a sample is loaded, FL marks it as tempo-synced only if the file's loop information says it is not
  a one-shot AND this option is on. With the option off the file's tempo information is not used.
- "Read sample root note": same place, for the root note in the file. LYNX does not read a root note from sample
  files at all, so there is nothing to switch (left out, listed in 3).

### 1.6 General › Undo history [STATIC: form + handlers]
- "Show recent changes at the top", default off: sets the order of the history list and refreshes it. Hint: sorts
  the history from new to old.
- "Undo knob tweaks" (default on) and "Show undo history in the browser when undoing changes": flags found, readers
  not studied.

### 1.7 Project › Info › "Reset working time" [STATIC: handler]
- The button sets the project's start date to now and restarts the working-time count; it asks nothing.

### 1.8 Already in LYNX (inventory corrections)
- "Manage messages › Show all / Show none": LYNX's Manage messages menu has both (ui/Dialogs.cpp).
- The underrun count: LYNX's Audio page status line shows "{n} dropouts (buffer underruns) so far".
- "Silent startup": see 1.4.
- "Play truncated notes", "Reset plugins on transport", panning law, time signature, Info fields and "Show info on
  opening" were already honoured, with their .flp events (rounds 167l, 190ax and earlier). No new project-level
  event was needed this round: every option of this round is an app option in FL too.

## 2. What LYNX does now (core/FlSettings.h)

All nine are app options, as in FL: kept in `ui-options.txt` (keys `fl…`), FL's defaults, so an old profile and an old
project behave exactly as before. Nothing is written to a project.

| FL option | LYNX page | Effect | Match |
| --- | --- | --- | --- |
| Resampling quality | Audio › Mixer, first row | The interpolation of sampler and Slicex channels while playing (2-point linear … 512-point sinc, FL's nine choices under FL's three headings). A note appears from 64-point on (FL shows a message box; LYNX keeps it inline). Export keeps its 32-point sinc | exact for the list and default; the note is LYNX's form of FL's warning |
| Multithreaded generator processing | Audio › CPU | Off: channels render on the audio thread only | exact |
| Multithreaded mixer processing | Audio › CPU | Off: mixer tracks render on the audio thread only | exact |
| Smart disable | Audio › CPU | Off: no effect slot is put to sleep, whatever its own switch says | exact for effects (LYNX's smart disable is effect-only, round 139) |
| Auto close | Audio › Output, under the device buttons | In the background with nothing playing, recording, armed, exporting or measuring: the device is closed; back in front (or needed again): opened again. A device LYNX did not close itself is never opened from here | approximate: FL's condition is "transport stopped"; LYNX adds armed inputs and exports to "busy". Needs a live check on a real device |
| Restore previous state after solo | General › Editing | Channel rack and Playlist track solo remember the mute states from before the first solo; un-solo restores them when the option is on | exact for channels (incl. "kept on a moved solo" and "read at un-solo"); Playlist tracks follow the same rule (FL's hint names channels, playlist and mixer tracks and effect slots; only the channel routine was read). LYNX's mixer solo is a state of its own that never rewrites the mutes, so it already restores; effect slots have no solo in LYNX |
| Auto name channels | General › Editing | Off: loading a sample into a named channel keeps the name | partial: the sample half; preset names not touched |
| Read sample tempo information | General › Editing | Off: samples loaded from then on carry no beat count (loops are fitted by length / detection) | exact in effect |
| Show recent changes at the top | General, with the undo options | The History panel lists newest first | exact |
| Reset working time | Project › Info | Start date = now, working time = 0, project marked changed | exact |

The Project and Info pages are now scrolling pages like the other four, so they fit the minimum window.

### Best way?

**Resampling quality.** FL: one global list, nine choices, a warning for the heavy ones. Ableton: no global list;
a per-clip "HiQ" switch (on by default in recent versions) for sample-rate conversion. Users: FL users know the list
and change it mostly by accident or for old machines; the common complaint is aliasing at "linear" forgotten on.
Verdict: FL's list as is (parity, and LYNX's kernels already cover all nine), default 24-point, plus an inline note
instead of a modal box. A per-channel override would be the better design, kept as an option for a later round.

**Multithreading switches.** FL: two global boxes plus per-plugin switches. Ableton: one "multicore" behaviour, no
switch in current versions. Users: the switches are a troubleshooting tool (a plugin that crackles when threaded).
Verdict: keep FL's two boxes, default on; they change no sound (tested bit-identical).

**Smart disable (global).** FL: master box plus per-slot switch plus a macro to set all slots. Ableton: devices
sleep automatically, no switch. Verdict: FL's master box; off is the "something cuts my reverb tail" escape hatch.

**Auto close.** FL: off by default, frees the driver when FL is not in front. Ableton: no such option (a similar
effect only with shared-mode drivers). Users: wanted with exclusive ASIO drivers to hear a browser or another DAW.
Verdict: FL's option, off by default, and never while anything is armed or running (FL's "stopped" rule widened so
a take or an export in the background is never cut).

**Restore previous state after solo.** FL: off by default (un-solo switches everything on). Ableton: solo is a
separate state, mutes are never touched, so nothing needs restoring. Users: Ableton's way is the one people praise;
FL users switch this box on early. Verdict: FL's option with FL's default (parity). Making "on" the default is the
better way and is offered to the user as a decision, not taken here.

**Auto name channels / Read sample tempo information / newest first / Reset working time.** Plain FL parity; no
better known way. Ableton has no equivalent of the first and reads tempo information under an "Auto-Warp" preference
of its own; its undo history was not studied for this round.

## 3. Left out, with the reason

Not built (LYNX cannot honour it today, or the FL behaviour was not established; no dead switches):
- Timebase (PPQ) choice and steps per beat: both are compile-time constants of LYNX's time model.
- Browser preview mixer track: LYNX's preview player is its own path next to the mixer.
- Auto name effect slots, Auto zip empty channels, Auto zoom in piano roll, Alternate meter scale, Small scrollbars,
  Hide plugin window toolbar by default, Undo knob tweaks, Fast sample preview, Auto keep long audio on disk,
  Threaded project sample loading, Initialize controls automatically: FL's behaviour not studied this round.
- Show undo history in the browser: LYNX's history is its own panel, not a Browser tab (needs a decision).
- Read sample root note: LYNX reads no root note from sample files.
- Project data folder, Rename project, Copy used samples to the project folder, Backup folder choice: project-folder
  model work, a round of its own.
- MIDI: velocity link targets and editable curves, Toggle on release, user port numbers, Auto accept detected
  controller. Theme: Light mode switch, delete preset, clips use note colours. Mouse / touch options.

Out of scope (Windows / driver / FL-internal; meaningless in LYNX):
- DirectSound boxes (Use hardware buffer, Use polling, Use 32-bit buffer, buffer offset) and ASIO boxes (Mix in
  buffer switch, Triple buffer, clock source): options of FL's own driver layers; LYNX uses JUCE's device layer.
- Safe overloads, Priority, Align tick lengths, Playback tracking mode / offset: internals of FL's mixing thread.
- True PPI, pop-up / toolbar / cursor / rounded scaling, Thick lines, Transparent menus, Force refreshes, Surface Pro
  hack, Don't limit windows to screen, High visibility: FL's own window toolkit.
- Force high performance power plan, Send usage data, automatic download of updates, FL Cloud, MIDI script update /
  edit / folder, external audio editor launcher.

## 4. Needs a live check
- Auto close on a real exclusive device (ASIO / WASAPI exclusive) on Windows: close in the background, reopen in
  front, no click, no lost device selection. And whether FL also closes after a plain "stop" while in front.
- FL's Playlist track solo with "Restore previous state after solo" (assumed equal to the channel routine).
- FL's "Auto name channels" for presets.
