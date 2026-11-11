# Round 190aw: MIDI controller layer and built-in profiles (phases A and B of REA_rodada190as §3.3)

Owner decision D2 (2026-11-11): controller profiles first, then the local control API (issue #75). This round builds
the layer every later phase sits on and the first profiles. Format reference: docs/CONTROLLERS.md.

Proof tags: [UI] a screen shot, [BIN] / [MAN] / [FILE] as in REA_rodada190as §3.1, [DOC] a maker's public document.

## 1. What FL has (summary of REA_rodada190as §3.1)

- Options › MIDI settings › Input: a device list with a second column for each device's controller type; under it
  **Enable**, **Controller type** (a list: "(generic controller)", then the device scripts, the user's own marked
  "(user)") and **Port**. One type per input device. [UI /mnt/e/lynx-work/flgap187a/shots/SHOT-midi-footpedal.png]
  [BIN list labels `(user)` / `(factory)`]
- A device script is a Python file (`device_<name>.py`); FL matches a device to a script by name or by its SysEx
  identity reply and offers it as that device's controller type ("Auto accept detected controller" in the same
  panel). [BIN][MAN]
- "Update MIDI scripts" next to "Refresh device list". [UI]
- Links made by hand (Link to controller, global links) and "Pickup (takeover mode)" live in the same panel. [UI]

## 2. What LYNX has now

- **Controller layer** (`core/ControllerSession.h`): one `Surface` interface (transport, mixer bank of 8, Channel
  Rack, plugin parameter pages, pads as notes / steps), sessions that turn device bytes into Surface calls and Surface
  state into feedback bytes (lights, motor faders, encoder rings, SysEx text), soft takeover for controls without
  motors, groups for a surface with extenders.
- **Profiles as data** (`core/ControllerProfile.h`, `*.lynxctl`): built-in files in `resources/controllers/`, the
  user's in the settings folder's `Controllers` folder.
- **Settings › MIDI › Input devices**: each device row has a **Controller type** list (Detect automatically,
  Generic controller, the profiles; the user's with "(user)"), where FL has it: with the input device. "Refresh
  device list" also reads the user's profiles again (FL's "Update MIDI scripts"); "Controller profiles folder" opens
  the folder.
- **Links made by hand keep priority**: a CC with a project or global link is never taken by a profile, on the MIDI
  thread (`Claims::takes`) and again in the session (`Surface::linkedByHand`). A controller a profile uses can still
  be linked: MIDI learn sees every CC first.
- A device with no matching profile, or set to Generic controller, is not touched: its messages take the same path
  as before this round.

## 3. Best way?

- **FL:** one Python script per device, matched by name / identity; a script can do anything, and a new device
  needs code. FL's own manual calls scripting a tool for hardware makers and advanced users. [MAN "MIDI scripting"]
- **Ableton:** Preferences › Link, Tempo & MIDI has Control Surface slots, each with an Input and an Output port
  chooser, and a global Takeover Mode (None / Pickup / Value Scaling). Besides its Python remote scripts, Live reads
  a plain text map per device (`User Remote Scripts/<name>/UserConfiguration.txt`: CC numbers for 8 encoders, 8
  faders, transport, pads), so a user can add a device without code. [MAN Live manual, "MIDI and Key Remote Control"
  and "Control Surface Setup"]
- **Users:** Image-Line's forum has repeated requests for devices nobody scripted yet and users who "wouldn't know
  where to start" with a script (thread "Novation SL Mk3"); a keyboard without a script is commonly run with FL's
  Mackie Control Universal type; script trouble is hard to diagnose for a user (thread "FL Key 49 - scripting
  failure"). Sources: https://forum.image-line.com/viewtopic.php?p=1668733 ,
  https://forum.image-line.com/viewtopic.php?p=2074733 ,
  https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/midi_scripting.htm
- **Verdict:** FL's placement and names (Controller type per input device, generic controller, "(user)", links and
  pickup in the same panel) are kept. For the first devices LYNX uses data files instead of scripts (Live's
  second mechanism): they cover the protocols with public documents, cannot run code, report mistakes by line, and
  users can add a device with a text editor. FL-compatible device scripts stay phase D on the same Surface, as an
  addition. One addition to FL's list: "Detect automatically" as the first entry (FL has a separate "Auto accept
  detected controller" switch); it is the default, so a known device works when plugged in.

## 4. Decisions taken (recommended option, for the owner to confirm)

1. The **focused plugin** is the selected channel's instrument (hosted plugin: its parameters; LYNX engines with a
   parameter table: that table). Effect slots are not paged yet.
2. **Soft takeover is always on** for a profile's absolute controls (a bank change makes jumps certain); the panel's
   "Pickup (takeover mode)" switch keeps governing hand-made links only.
3. `transport.loop` is the pattern / song switch (FL's "loop mode"), lit in pattern mode.
4. A profile's messages skip the device's "one MIDI channel only" filter (a control surface uses several channels).
5. Mackie HUI was not built: its zone / port select scheme and ping need a state machine, not a table.
