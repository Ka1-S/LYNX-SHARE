# REA round midiout190jl: how FL Studio 2026 sends MIDI to hardware outputs (FLEngine_x64.dll)

This is a clean-room study. It records behaviour and names only, and copies no code.
Sources: a Ghidra 12.1.4 headless copy of the flgap187a project (FLEngine_x64.dll, image base 0x400000), objdump, the
DFM/RTTI strings, and objdump of `Plugins/Fruity/Generators/MIDI Out/MIDI Out_x64.dll`.
Tags: [DECOMP addr] means read in the decompiler or disassembly. [STRINGS] means DFM, RTTI or literal text.
[HYP] is an inference.

## Data model (needed for every answer)

- **The output-device record** is class `TMIDIOutDevice`, VMT 0xfd5f50, constructor 0xfdc710. It sits in a global
  array (pointer slot 0x15e0660). The fields:
  - `+0x08` is the paired input's controller type, or −1. The constructor sets −1. VMT slot 0 (0xfdca00) is the setter.
  - `+0x0c` is the device kind: 2 = a WinMM device, 0 = "Internal".
  - `+0x10` is the name and `+0x18` is the IDString.
  - `+0x30` is the **Port**.
  - `+0x3c` is "opened".
  - `+0x40` is the **paired input**.
  - `+0x48` is **Sync** (send master sync).
  - `+0x49` is **SendAllNotesOff**. The constructor sets it to **1** (on by default).
  - `+0x4c` is the WinMM wrapper object (HMIDIOUT at wrapper+0x7c).
  - `+0x54` is a copy of the handle.
  [DECOMP 0x117ca10, 0xfdc710, 0xfdc830, 0x117e9f0]
- **The array layout**: index 0 is WinMM device id −1 (MIDI Mapper). Indexes 1..N are the WinMM outputs. When FL runs as
  a plugin (flag 0x16bc9ef), one extra device named "Internal" comes last. Its port defaults to 10 and it routes to the
  host DAW. [DECOMP 0x117cdb0, 0x117ca10]
- **The input-device record** (VMT 0xfd57d8; the scripted variant is VMT 0xd70370) has these fields: `+0x08`
  controller type, `+0x20` LinkedOutName, `+0x30` Port, `+0x7c` **paired output**, `+0x8c` Enabled.
  [DECOMP 0x117e9f0, 0xfd8550]
- **The active-output list** (pointer slot 0x15e4560) is rebuilt in 0x117bf10. An output goes in when it is opened,
  is a hardware kind (`+0x0c > 1`) and the global **Options › "Enable MIDI output"** is on (byte 0x16bcc7e).
  [DECOMP 0x117bf10; menu action `OptionsEnableMIDIOutputActionExecute` 0xf1e220; 0x11d3380 writes 0x16bcc7e]

## Q1. The output Port: range, default, storage, OutPortSelRelease

- **Range and default.** The control is `OutPortSel`, a `TVectorDigiWheel`. Its DFM has `Min = −1`, `Max = 255`,
  `Value = −1` and `DefaultValue = −1`. The DFM hint is "|Access port number". The theme hint strings "|Output port
  number" and "|MIDI output port number" also exist. **−1 means no port.** The input wheel `InPortSel` has the same
  range and the hint "|Unique / feedback port number". [STRINGS DFM 0x1bc0f61, 0x1bbed2d] High confidence.
- **The default when devices are enumerated.** Each output starts at −1. If an input device "matches" the output, the
  output takes **that input's port**. 0xfd9430 decides the match: the output name equals the input's
  `LinkedOutName`, or a name heuristic for that controller type fits (for example "LAUNCHKEY" + "MIDI", "IMPACT").
  The saved registry value then overrides this, but only if it is ≥ 0. [DECOMP 0x117cdb0 lines ~470–525, 0xfd9430]
  High confidence.
- **Auto-assigned ports.** When FL detects a controller that has a script, it switches **Enable MIDI output** on
  (0x16bcc7e = 1). If the input's port is < 0, FL gives it a free one: 0xfd7650 searches upward for a port that no
  other device uses and wraps 255 → 0. With the name match above, the controller's input and output then end up on
  the same port. [DECOMP 0x117cdb0 ~line 399, 0xfd7650] High confidence.
- **Storage.** Values go in the registry under `<Devices>\MIDI output\<device name>`: `Port` (int), `Sync` (bool),
  `SendAllNotesOff` (bool) and `IDString`. Inputs use `...\MIDI input\<name>`: `Port`, `Type`, `Enabled`,
  `ScriptFolder`, `IDString`, `ConnectionCounter` and `LinkedOutName`. When two devices share a name, the later one
  gets a numbered suffix. [DECOMP 0x117e9f0 (save), 0x117cdb0 (load)] High confidence.
- **`OutPortSelRelease`** (0x135d3f0) runs when a selected output row exists (form field 0x12ac ≥ 0):
  1. If the new port is ≥ 0 and "Enable MIDI output" is off, it **switches Enable MIDI output on**. It does this
     through the options interface, vtable +0x18 with the value 1.
  2. It calls "Opening MIDI output devices" (0x117ca10) with the selected index. That function:
     - stops the transport;
     - copies `OutPortSel` → `+0x30`, `MasterSyncBox` → `+0x48` and `SendAllNotesOffBox` → `+0x49` into that record;
     - for every output: **opens** it when (Enable MIDI output and port ≥ 0) or (master-sync mode 0x16bcc80 == 1 and
       Sync), and **closes** it otherwise;
     - rebuilds the active list and the input/output pairs (0x117bf10).

  [DECOMP 0x135d3f0, 0x117ca10] High confidence.
  `SendAllNotesOffBoxClick` (0x135cd70) only stores `+0x49` and does not reopen anything. [DECOMP]

## Q2. Dispatch of short messages carrying a port

- **The internal word.** The low 24 bits hold the MIDI status and data bytes. The **high byte is the port**.
  The MIDI Out plugin builds it as `status|channel + data·… + port<<24`; see round 16, `FUN_006fec00` in
  MIDI Out_x64.dll. [DECOMP from round 16]
- **The host entry.** MIDI Out sends **every** message through host-callback vtable **slot +0x48** (TFruityPlugHost
  VMT 0x13141b8 → **0x132a950**). It has about 25 call sites with `call [reg+0x48]` and none for other MIDI slots.
  [DECOMP objdump MIDI Out_x64.dll 0x6f80f8…0x700730] High confidence. Slot +0x50 (0x132ab80) is an immediate
  variant: it takes the lock and calls the dispatch straight away. MIDI Out does not use it.
- **0x132a950 does this:**
  1. Under a lock, it offers the word to **every other plugin**. The sender is skipped, and that is the only identity
     check. If a receiver sets the word to −1, the message is consumed and stops there. Delivery to wrappers whose
     MIDI input port matches is round 16's finding. [DECOMP 0x132a950]
  2. If FL is **not rendering** (byte 0x16baff0 == 0) and the port equals the "Internal" (FL-as-plugin) port, the
     message goes to the plugin host with a sample offset.
  3. Otherwise it goes into the **time-stamped queue** (0xbb0840, see Q3). If the queue is not running, it goes to the
     **immediate dispatch 0xbb0400**.
- **The dispatch 0xbb0400** walks the active-output list. For **every output whose `+0x30` port == (word >> 24)**, it
  sends `word & 0xFFFFFF`:
  - through `midiOutShortMsg` (wrapper 0xba4a20 → winmm; the only caller of the import thunk 0x663430), or,
  - when the output has a paired input, through that input's vtable +0x58 (0xfd9180). That forwards to the input's
    paired output, which is the same device again, and ends in `midiOutShortMsg`.

  [DECOMP 0xbb0400, 0xba4a20, 0xfd9180, 0xfdc920] High confidence. **Yes: one message reaches every opened output
  that has the same port.**
- **Message types.** There is **no type filter** in FLEngine. Any short message is passed through as-is: note on/off,
  CC, program change, pitch bend, channel and poly aftertouch, and also system messages if a plugin builds them.
  **The channel is the message's own** (the status low nibble). FL does not rewrite it.
  [DECOMP 0xbb0400] High confidence.
- **SysEx.** It goes through the host **Dispatcher (vtable slot 0, 0x13173e0) with opcode 0x29 (41)**. That opcode
  walks the same active-output list. For each output with port == Index, it calls `midiOutLongMsg` (wrapper 0xba4a40:
  prepare header + long message). The buffer is `[u16 length][bytes]` at `Value+4`. SysEx is **immediate** and not
  queued. [DECOMP objdump 0x13189e5 → 0x131c4dc…0x131c57a, 0xba4a40] High confidence about the behaviour. The name
  "FHD_SendSysEx" for opcode 41 comes from public FL SDK memory and is [HYP] for the name only.

## Q3. Timing

- **Queue.** The words are **time-stamped and queued**, not sent from the mixer thread. 0xbb0840 adds
  `{stamp = current mixer tick (int at 0x196ec48), word}` to a locked list (0x16b6d60). It only does this while the
  **MIDI output thread** is running (thread object 0x16b6d30, `+0x58 == 1`). [DECOMP 0xbb0840, 0xbb1400]
- **The thread** is TThread 0xbb0920 → loop 0xbb0b00. It runs at "Pro Audio" MMCSS priority and wakes every **1 ms**
  (`Sleep(1)`).
  - On each pass it reads the **audio-played position**, converted to ticks (0x1311db0 → 0x1311a40). That position
    is the driver's played sample count, interpolated with QueryPerformanceCounter between callbacks, or the
    DirectSound play cursor.
  - For each tick it has passed, it bumps the counter (0x196ec4c) and **sends every queued message whose stamp ≤ that
    counter** (0xbb04a0 → 0xbb0400).
  - The same loop also drives the MIDI clock, SPP and MTC for the master-sync outputs.

  [DECOMP 0xbb0920, 0xbb0b00, 0xbb04a0, 0x1311a40, 0x1311bf0, 0x1311e90] High confidence.
- **Effect.** MIDI goes out **when the audio for that tick is actually heard**. The output latency is compensated
  implicitly by the audio clock, and the time resolution is one PPQ tick (samples per tick = 0x15d8980, from
  0x1311510). FL has no separate user "MIDI output latency" setting in this path. [DECOMP; HYP for "no other
  setting": none found]
- **Start and stop.**
  - The thread starts timing when the transport state becomes playing (0x11c8f60 → 0xbb1290), and **not while
    rendering** (0xbb1290 checks 0x16baff0).
  - On Stop, 0x11ca290 → 0xbb12d0 clears the running flag and 0xbb1360 sends 0xFC to the sync outputs.
  - While the thread is not running, MIDI Out messages go **out at once** (0xbb0840 returns false → 0xbb0400), for
    example notes played live while stopped.
  - When the thread is destroyed (0xbb06d0 → 0xbb0600), the queue is flushed with stamp 0x7fffffff, so everything
    left is sent.

  [DECOMP]
- **Master sync Offset.** `MasterSyncOffsetChange` (0x13678e0) writes **one global** (slot 0x15e05e8). That global is
  read only in the clock part of 0xbb0b00. **It is not applied to notes**: neither 0xbb0400 nor 0x132a950 uses it.
  [DECOMP] High confidence.

## Q4. Feedback port and echo

- **What the input port does.** The input's port ("Unique / feedback port number") **pairs** the input with the
  opened hardware output that has the same port (0x117bf10, through 0xfd8550 / 0xfdc9c0):
  - input `+0x7c` = output, and output `+0x40` = input;
  - output `+0x08` = the input's controller type.

  A pair forms only when both devices are hardware kinds, the output is opened with port ≥ 0, and the input is enabled
  and not paired yet. Its purpose is **controller feedback**: script calls such as `device.midiOutMsg` (0xd747f0,
  format "i|iiii", error −6 when no output is paired), `midiOutSysex` and similar send to `input+0x7c`, the paired
  output (0xd84a00, 0xfd9180, 0xfd9930, the 0xf9xxxx–0xfcxxxx script API). [DECOMP] High confidence.
- **No echo guard.** I found **no explicit loop or echo guard** in the hardware path. The dispatch 0xbb0400 compares
  only the port, and the message carries no source-device identity. So when an input plays a MIDI Out channel set to
  port P, FL sends the notes to **every** output on port P, including the output paired with that same input.
  The only self-guard is "skip the sender plugin" in the plugin broadcast (0x132a950).
  [DECOMP 0xbb0400, 0x132a950] Medium-high confidence: an absence, checked on both send paths.
- **One indirect protection.** The CC 123 "all notes off" panic goes only to outputs whose `+0x08 == −1`. Outputs
  paired with a **scripted** controller input are therefore skipped (see Q5). [DECOMP 0x11c89c0, 0xfdca00] Medium
  confidence on what the type value means: −1 = generic or none.

## Q5. Stop, panic and render

- **The "Send all notes off" sender 0x11c89c0.** For each active output with `SendAllNotesOff` (`+0x49`) and
  `+0x08 == −1`, it sends **CC 123 value 0 on all 16 channels** (word 0x7BB0+ch). It sends **no note-offs of held
  notes and no CC 64**. [DECOMP 0x11c89c0] High confidence.
- **Who calls it:**
  1. **"Stop sound"** (`MacroStopSoundActionExecute` 0xf1de20 → 0x11c8d10) and **Stop pressed while already stopped**
     (transport command 0x11ca520 with cmd 0 when stopped → 0x11c9de0 → 0x11c8d10). Each also kills voices and resets
     plugins (0x11c8a40(1,1,1)).
  2. Moving the song position or stopping (0x11e7a10), **only when Audio › "Reset plugins on transport"**
     (`PlugResetBox`, hint "Uncheck for faster response when relocating") is checked.
  3. 0xf46490, another caller whose decompile timed out.

  [DECOMP 0xf1de20, 0x11c8d10, 0x11ca520, 0x11c9de0, 0x11e7a10; STRINGS PlugResetBox caption]
- **A normal Stop.** It kills the voices (0x11ca290 → 0xd2a500 and others). MIDI Out's own note-offs then go out at
  once through the host, because the thread is stopped. FLEngine itself sends no extra CC 123 on a plain Stop unless
  "Reset plugins on transport" is on. [DECOMP; HYP that the note-offs come from MIDI Out's voice release]
- **The DFM hint** for SendAllNotesOffBox reads "|Send all notes off to this device **on init and after render**".
  [STRINGS 0x1bc16d2] I did **not** locate the exact after-render call: render end (0xd1af20) calls 0x11c8a40(1,1,1)
  and clears the render flag, but no direct call to 0x11c89c0 was found there. Treat the hint as the spec.
- **During render** (0x16baff0 = 1, set in render start 0xd18130 and cleared in render end 0xd1af20):
  - 0x132a950 sends **nothing to hardware**;
  - the MIDI thread is not started;
  - instead, for one plugin class with an attached object (at +900), 0x132a950 writes the event with its tick into
    that object. [HYP: this records MIDI Out events into an exported MIDI / plugin buffer]
  - **No hardware MIDI output during export.** [DECOMP 0x132a950, 0xbb1290] High confidence. The SysEx dispatcher
    path was not checked for the render flag.

## Q6. "%s TO output port %d"

- This is **not a log and not a MIDI monitor**. In FLEngine it only exists in the shared, sorted translation
  dictionary (0x146413c). No code references it. [STRINGS]
- The UTF-16 literal that is actually used lives in **ReWired_x64.dll** (the ReWire plugin) at file offset 0x2efae2,
  next to `Input port %d TO "%s"`. It is a ReWired routing label. The copies in ControlCreator.exe and
  PluginManager.exe are dictionary copies too. [STRINGS] High confidence. It does not matter for hardware MIDI out.

## Still unknown

- **Which plugins consume the word** in 0x132a950's broadcast (setting it to −1, which then blocks the hardware send).
  The receiver is reached through two interface hops (0x1331380 → +0x20 → +0x80), not resolved.
- **The exact after-render "all notes off" call** (see Q5), and the role of 0xf46490.
- **The meaning of the master-sync mode global 0x16bcc80** (== 1 opens Sync outputs). It is probably the "Enable MIDI
  master sync" option, which the action at 0xf1e1e0 calls through vtable +0x20. [HYP]
- **The exact tick unit**: samples per tick 0x15d8980 = 15·SR / (tempo·k). That it is the project PPQ tick is [HYP].

## What this means for LYNX

- A port-matched send goes to every output on that port. The channel is not rewritten and there is no type filter.
  SysEx goes to the same port set.
- Notes are time-stamped and sent when the audio is heard. LYNX already does this for sync: block time + device
  latency. The master sync offset does **not** apply to notes. When stopped, sends are immediate. Nothing is sent
  during export.
- A device's input port and output port pair the two for controller feedback. There is no echo guard. LYNX could
  offer one as an option, but FL-exact is "none".
- "Send all notes off" is CC 123 ×16 only. It defaults to **on**. It fires on Stop-while-stopped, on "Stop sound", on
  stop or relocate when "Reset plugins on transport" is on, and on init and after render (from the hint). It skips
  outputs paired with a scripted controller.
- Ports run from −1 (none) to 255. The registry keys are Port / Sync / SendAllNotesOff. Choosing a port turns "Enable
  MIDI output" on.

## What LYNX does (round 190jl) [CODE]

- `prefs::MidiOutputPrefs::port` (−1…255, none by default), saved as its own `outputport=` line (older versions skip
  it). `MidiPrefs::outputPort`: the device's own port, else the port of the input that is the same device (the same
  name or `controller::pairedOutput`), as FL's enumeration gives it.
- The MIDI Out channels and the plugins whose wrapper output port an output device has send what they play:
  - the MIDI Out instrument (`core/MidiOut.cpp`) keeps its notes (its MIDI channel, or the note colour with "Map note
    color to MIDI channel"), the controllers / pressure / program a MIDI input plays into it, with their sample in the
    chunk; its knobs, patch, bank, pan / volume / pitch (the reset on play) are taken at the chunk's start;
  - a hosted plugin (`engine/PluginHost.cpp`) keeps every short channel message of its MIDI output;
  - `RackRenderer` collects them per block in time order (`takeHardwareMidi`); the engine hands them to `MidiSyncIo`
    after each device sub-block, due at the block's start + one buffer + the device's output latency + the sample:
    sent when the audio is heard, as FL's MIDI thread does. The master sync offset stays the clock's.
  - no message type filter, the channel is the message's own; nothing during an export (the live callback does not
    render then).
- Stop / panic: the MIDI Out channel ends its notes (note offs) and the sender ends any note a port device still
  sounds (a LYNX safety net). FL's CC 123 bursts ("Send all notes off"): on Stop sound, on Stop while stopped (both
  added this round), on a stop / relocate with "Reset plugins on transport" (round 167l), on init and after a render.
- **LYNX extra (not FL):** a device does not get back what its own input played into the MIDI Out channel (FL has no
  echo guard). Every live note / control carries its input's number (`NoteEvent::source`).
- Not done: SysEx to the port's devices (FL's dispatcher opcode 41); a plugin consuming the message before the
  hardware; skipping outputs paired with a scripted controller for CC 123.
