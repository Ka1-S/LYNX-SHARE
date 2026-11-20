# REA round 190ks: what FL Studio 2026 does with a hosted plugin's MIDI output, and how native GUI notes get recorded

This is a clean-room study. It records behaviour, addresses and names only, and copies no code.
Sources: headless Ghidra 12.1.4 on copies of the flgap187a project (FLEngine_x64.dll, image base 0x400000) and the r134w
project (Fruity Wrapper_x64.dll from Plugins/Fruity/Generators, called "fw" below, base 0x400000). Also objdump of
FPC_x64.dll, FL Keys, Fruity Keyboard Controller and Slicex, and a Python lister of Delphi VMTs and published-method
tables. (For this build, vmtSelfPtr is at −0xC8 and the class name at −0x88.)
Tags: [DECOMP addr] means read in the decompiler or disassembly. [STRINGS] means DFM, RTTI or literal text.
[HYP] is an inference. Confidence is given per claim.

## Short answers

1. **Plugin MIDI out is only forwarded and is never recorded.** A note that a hosted VST2, VST3 or CLAP plugin emits
   goes only to the wrapper's MIDI **Output port**, through host slot +0x48 (FLEngine 0x132a950). That slot offers the
   note to other plugins' `MIDIIn`, then to the hardware outputs on that port.
   - Nothing feeds FL's live-note or recording path. FL's Record › Notes does **not** record a plugin's MIDI output.
   - With Output port = −1, the wrapper drops the event and does nothing else.

   High confidence.
2. **FL has a dedicated host opcode that plays a note "as if the user played it" and records it: Dispatcher
   (slot 0) opcode 0x38 (56).** It calls FLEngine 0x10002e0, the same function the MIDI keyboard and the PC typing
   keyboard reach (through 0x1181620). That function records the note when Record › Notes is armed, then plays it.
   - Opcodes 0x14 / 0x15 (20 / 21) are "play note on / off" with **no** recording. FL Keys, Fruity Keyboard
     Controller and Slicex use them for their own on-screen keyboards.
   - **FPC's pad click calls none of these.** It starts an FPC-internal voice under LockPlugin / UnlockPlugin only.
     So, statically, FL cannot record an FPC pad click.

   High confidence on the opcodes. Medium-high on the FPC conclusion, which contradicts the brief's premise and needs
   a runtime check.
3. **No wrapper setting routes plugin MIDI out into recording.** The wrapper has Input port / Output port
   (−1…255), "Send note release velocity", "Record parameter changes as automation" (parameters only) and
   HostKeyboardHandling (PC keyboard forwarding).
   - FLEngine's **"Receive notes from"** is a different feature. It is a Channel rack menu that locks a channel to a
     **hardware** MIDI input port and channel. Plugin MIDI out never reaches it.
   - I found no "Send note events to" string. High confidence.
4. (Extra question) **Every channel, native or wrapped, has a hardware "Receive notes from" port at `channel+0x2f8`**,
   plus a MIDI channel at `+0x2fc`. `getChannelMidiInPort` returns `+0x2f8`. The plugin MIDI-out broadcast does
   **not** reach a native generator through it. It reaches only plugins whose own `MIDIIn` checks the port.

## Q1. What the Fruity Wrapper does with a plugin's MIDI output

### The class layout [DECOMP + VMT/RTTI]

- The plugin-format classes are TVSTPlugin (VMT 0x7a77c8), TVST3Plugin (0x841b90) and TCLAPPlugin (0x859718). All
  derive from **TBaseAudioPlugin** (0x743ad0).
- The plugin's host object is at `plugin+0x78`, of class **TAudioPluginHost** (VMT 0x867d98, parent
  TAudioPluginHostIf 0x743840).
- The FL-side wrapper is **TFruityWrapper** (VMT 0x8685d8; TGeneratorWrapper 0x868840 derives from it).
- In the wrapper, the FL host (TFruityPlugHost) pointer is at `wrapper+0xbc` and the HostTag at `wrapper+8`. The MIDI
  ports are `wrapper+0x854` (input) and `wrapper+0x858` (output). Both are −1 by default (0x86aac0 and 0x874460 write
  −1) and both are saved (0x8757b0). This matches round 5 / round 16. [DECOMP]

### VST2 [DECOMP]

The host callback is 0x7a8620, and the opcodes it does not handle itself go to 0x7abb00.

1. **audioMasterProcessEvents (opcode 8)** goes to **0x7b1a00**. For each VstEvent:
   - `kVstMidiType` (1): the plugin's virtual `+0x28` with `(midiData[0..2] as a 24-bit word, 1, −1)`;
   - `kVstSysExType` (6): the plugin's virtual `+0x30` with `(0, size, data)`;
   - the other types are ignored. **`deltaFrames` is ignored** (the sample offset passed is −1).
2. `TBaseAudioPlugin+0x28` (0x745810) goes to `host(+0x78)->+0x20`, which is **TAudioPluginHost 0x878600**.
   `+0x30` (0x746060) goes to `host->+0x28`, which is 0x878730 → 0x8777d0 (SysEx).
3. **0x878600, the only MIDI-out sink:**

   ```
   if (wrapper.outPort(+0x858) >= 0) {
       word = msg24 | (outPort << 24);
       if (flag) FLhost->slot+0x48(HostTag, word)   // MIDIOut: broadcast + hardware, time-stamped
       else      FLhost->slot+0x50(HostTag, word)   // immediate variant
   }
   // nothing at all when the port is < 0
   ```

   All three formats pass flag = 1, so the slot is **+0x48 = FLEngine 0x132a950**. This is the only reference to the
   output port in the send path. Other uses of 0x858 are the setter 0x873100 (stores it and refreshes the UI), save
   and load, and the getter 0x87a250.

**SysEx** (0x8777d0): when `size > 0` and data is present, it calls host **Dispatcher opcode 0x29** with
Index = the output port. There is **no** port ≥ 0 check here. FLEngine's opcode 0x29 sends to opened outputs whose
port equals Index, so −1 matches nothing. [DECOMP; FLEngine side from round 190jl]

### VST3 [DECOMP 0x84d3f0]

This runs after process() and walks the output event list (`plugin+0x8c4`):

- `kNoteOnEvent` (0) becomes `0x90|ch, pitch, round(velocity·127)` and goes to `+0x28(...,1,−1)`. The channel is
  clamped to 0…15.
- `kNoteOffEvent` (1) becomes `0x80|ch, pitch, round(offVelocity·127)` and goes to `+0x28`.
- `kDataEvent` (2) with type 0 (MIDI SysEx) goes to `+0x30`.
- Type 0xFFFF (`kLegacyMIDICCOutEvent`) goes to 0x84d270, which builds a short message. I did not read it in detail.
- Poly pressure, note expression and the sample offset are ignored. Same sink as VST2.

### CLAP [DECOMP 0x85d920]

- NOTE_ON (0) and NOTE_OFF (1) are converted the same way.
- CLAP_EVENT_MIDI (10) passes its 3 bytes through.
- MIDI_SYSEX (11) goes to `+0x30`.
- PARAM_VALUE (5) goes to the wrapper's set-parameter, which is not MIDI.

### What FLEngine does with the word (slot +0x48 = 0x132a950) [DECOMP]

It does exactly what round 190jl found:

1. Under a lock, it walks every plugin in the list at 0x196ed60 (skipping the sender). For each one it calls
   `FUN_01331380(obj) → iface+0x20 → plugin` and then **plugin VMT +0x80 = TFruityPlug.MIDIIn(&word)**. If a receiver
   sets the word to −1, the message is consumed.
2. If FL is not rendering and the port equals the FL-as-plugin "Internal" port, the word goes to the host DAW. That
   port's global 0x1542148 is initialised to −1 in .data, and the target object 0x15ce510 is written only in
   `CreateFruityInstance`.
3. Otherwise it goes to the time-stamped hardware queue (0xbb0840) or the immediate dispatch (0xbb0400).
4. While rendering, the word only goes into the plugin's own attached recorder object (`+900`, see 190jl).

**No branch reaches the live-note or recording functions** (0x1181620, 0x10002e0, 0x1000f10, recorder 0x1060320).
High confidence: the whole function was read, and its callees are 0x40fe90, 0x1331380, 0x9866b0, 0x986a80, 0x986bd0,
0xbb0400, 0xbb0840 and the locks.

### The receiving side: the wrapper's MIDIIn [DECOMP 0x872fd0]

TFruityWrapper VMT +0x80 is 0x872fd0:

```
if (word >> 24 == inPort(+0x854)) {
    queue word & 0xFFFFFF into the hosted plugin (0x871cb0)
    Dispatcher(HostTag, 0x22, 0, 0)   // FLEngine case 0x22: clears the plugin's +0xa0 counter when +0xf7 is set
}
```

[HYP] Opcode 0x22 is a "MIDI activity / wake from smart-disable" notification.

- The wrapper **never consumes** the word: it does not set −1. So a matched plugin and the hardware outputs both get
  it.
- The received note goes straight into the receiving plugin. It is not a channel note, so **it is not recorded
  either**.
- The input-port setter 0x873070 calls Dispatcher opcode 5 with Value = (port ≥ 0). [HYP] This is a "wants MIDI
  input" flag.

### Conclusion for Q1

- **Forward only.** Plugin MIDI out goes to Output port P, then to the `MIDIIn` of every other plugin and wrapper
  whose input port is P, and to every opened hardware output on P.
- It is **not** injected as live input, and FL's recorder never sees it.
- **Port −1: nothing happens**, except that SysEx still calls opcode 0x29 with −1, which matches no device.

High confidence.

[HYP, consistent with the code] FL users record plugin MIDI output through a loopback MIDI port. The plugin's Output
port goes to a hardware/virtual output, which comes back as a hardware input. That is the only way in this build for
the notes to reach 0xfdbe60 → 0x1181620 → the recorder.

## Q2. How native generator GUIs play and record notes

### The live-note path in FLEngine [DECOMP]

**Hardware MIDI input** enters at 0xfdbe60, a TMIDIInDevice method (VMT 0xfd57d8):

1. If the device port is ≥ 0, it first offers the word (port in the high byte) to every plugin's `MIDIIn`, the same
   loop as 0x132a950. A −1 result stops it.
2. It then builds an event and calls the device's `+0x40` (0xfdb260). For 0x80/0x90 that is **0xfdb0f0**.
3. 0xfdb0f0 calls either:
   - **0x1181620 `(note, vel, midiChan, devicePort)`**, the "play live note" function. Other callers of 0x1181620
     are 0xf0d3c0, 0xf0d430, 0xf0e680 and 0xf0e800 (main-form key handlers) and 0x11828c0 / 0x1182a20 /
     0x1182b30. [HYP] These are the PC typing keyboard paths.
   - or, when MIDI-channel → channel mapping is active, **0x1000f10 `(channel, note, vel, midiChan)`**.

**0x1181620** handles several things:

- the special "controller" MIDI channels (song/pattern selection, mute and other commands);
- the per-channel "Receive notes from" locks (see Q4);
- otherwise, the selected / focused channel.

Each of these ends up in **0x10002e0 `(channel, index, value, midiChan)`**. When the Index has bit 0x80000000, the
note number is in the low 16 bits, the MIDI channel is in bits 16–23, and Value is the 0–127 velocity (≤ 0 means
note off, with velocity −Value).

**0x10002e0 is the record-and-play entry:**

- **"Wait for input to start recording"** (0x15dfef8): it starts recording on the first note-on.
- **Step / typing-keyboard step entry** (0x15e1198): it writes the note into the recorder buffer.
- **Record › Notes** (`*0x15e4c98 & 2`): it takes the song position (0x1311dd0) and **records note-on / note-off
  into the recorder** (0x1060320 / 0x1060460 on 0x15e2318), keyed by channel id `+0x9c + 0x4000`.
  - When "Blend recorded notes" (0x15e2150) is off, it sets `channel+0x6ee`, which clears the pattern before the
    take. `OptionsBlendRecordedNotesActionExecute` 0xf1e0f0 toggles 0x15e2150.
- **Always**: it writes into a second note buffer, 0x15e3158. [HYP] This is the "score log" that "Dump score log"
  uses.
- **Then plays** the note: 0xff4e50 for note on, 0xff5100 for note off.
- **Index bit 0x40000000** skips all recording ("play only").

High confidence on the structure. The globals' UI names are [HYP] except Blend, whose action is decompiled.

0x1000f10, the mapped-channel variant, does the same "Record › Notes → 0x1060320 / 0x1060460 → play" sequence.

### Host Dispatcher opcodes for notes [DECOMP]

The Dispatcher is TFruityHost VMT slot 0 = 0x13173e0. Its switch is on ID at `[rbp+0x1870]` and was resolved by
emulating its compare tree. Sender is `[rbp+0x1868]`, Index `[+0x1878]` and Value `[+0x1880]`.

| Opcode | FLEngine | Effect | Recorded? | Who uses it |
|---|---|---|---|---|
| **0x14 (20)** | case at 0x131a106 | when the Sender is a generator channel (0x132f7d0 checks the class), calls 0xff4e50 `(chan, Index & 0xFFFF = note, packed(Value = velocity), −1, Index >> 16)`. This plays a note on that channel | **no** | FL Keys (0x724e7a), Fruity Keyboard Controller (0x6fecce), Slicex (0x8933a9) |
| **0x15 (21)** | case at 0x131a15d | 0xff5100 `(chan, Index & 0xFFFF, Value ∈ 0…127 else 64, −1)`. This is note off | **no** | the same three plugins |
| **0x38 (56)** | case at 0x131a1d8 | stores a timestamp (0x63a730 → 0x15e0e98), then **0x10002e0 `(chan, Index | 0x80000000, Value, −1)`**: the live-note path above, **recorded like the MIDI or typing keyboard** | **yes** | none of the scanned natives (FPC, FL Keys, Keyboard Controller, Slicex, Fruity Wrapper) |
| 0x11 (17) | case at 0x1319882 | takes a note list from the plugin and writes it with the recorder (0x1060320) into the pattern / piano roll | n/a | FPC's MIDI-loop "drag / send to piano roll" (0x8606c2); Fruity Wrapper TAudioPluginHost (0x878094) |

The SDK names are only memory here: 0x14/0x15 might be the SDK's note-on / note-off preview IDs, and 0x38 is a later
addition. The behaviour above is what the code shows.

### FPC pad clicks [DECOMP objdump + published-method table]

The handler is `TSynthEditorForm.BtnPadMainMouseDown` (0x859470); MouseUp is 0x859050.

- **Left button** goes to **0x870720**, which builds a velocity record and calls **0x86b480**, FPC's internal voice
  start (for each layer).
  - 0x86b480 calls the host only with **slot +0x100 / +0x108** (FLEngine 0x132b230 / 0x132b250 → 0x132fe10 /
    0x1330a10). [HYP] These are LockPlugin / UnlockPlugin.
  - The voice is FPC-internal: its host voice handle is −1.
- **Middle button** goes to 0x859680, which uses Dispatcher 0x58 and 0x3 (menus).
- A full scan of FPC for host Dispatcher calls finds the opcodes {2, 3, 0xd, 0xe, 0x11, 0x12, 0x13, 0x16, 0x1c,
  0x1d, 0x1f, 0x20, 0x2c, 0x2e, 0x30, 0x32, 0x3b, 0x47, 0x55, 0x58, 0x5b, 0x5d, 0x5e}. **There is no 0x14, 0x15 or
  0x38**, and FPC's own code has no MIDIOut (+0x48) call.

So, in this build, an FPC pad click is **not** routed to any FL path that records. Medium-high confidence: this is a
static absence over the whole binary.

**This contradicts the brief's "FPC pad clicks are recorded".** Please run a 1-minute runtime check in FL: Record ›
Notes, start recording, click FPC pads with the mouse, and see whether notes appear.

- If they do, the mechanism is outside FPC's dispatcher calls. One possibility is that FL itself watches the focused
  plugin window. I found no such code.

### What LYNX should mirror

The FL-exact native path for a GUI note that must be recorded is:

> plugin → host Dispatcher **opcode 0x38**, Index = `note | (midiChannel << 16)`, Value = velocity (> 0 on, ≤ 0 off
> with −velocity) → FLEngine 0x10002e0, the same function as the MIDI and typing keyboard

0x10002e0 does three things:

- **records** when Record › Notes is armed (blend / clear rules, wait-for-input, score log);
- **plays** the note on the sender's channel (0xff4e50 / 0xff5100);
- bit 0x40000000 means "play without recording". GUI previews that must not record use opcode 0x14/0x15 instead.

For LYNX: a hosted plugin's MIDI output is a different thing and FL does not record it. Recording it would be a
**LYNX extra**: feed plugin MIDI-out notes into LYNX's live-note / recorder entry for the plugin's own channel. That
needs an owner decision, because it is not FL-exact.

## Q3. Settings and strings

- **Fruity Wrapper**:
  - [STRINGS] `MIDIInPort`, `MIDIOutPort`, `MIDIInPortWheel`, `MIDIOutPortWheel`, `lblMidiInPort`, `lblMidiOutPort`,
    "Output port", "|MIDI output port number", "Send note release velocity";
  - "'|Record parameter changes as automation" (parameter automation, not notes);
  - `HostKeyboardHandling` / `HostKeyboardHandlingAfter` (PC-keyboard forwarding to the plugin editor);
  - "..\MIDI Out\Data\GM CCs.txt".

  None of them changes the MIDI-out routing. The only control is the Output port number (−1 = off), used by 0x878600.
  [DECOMP]
- **"Receive &notes from"** [STRINGS DFM]: it is in FLEngine, in the **Channel rack channel menu** as
  `LineLockControllerMenu`, next to "MIDI channel through" and `LineUnlockControllerMenu` "&Unlock". Its submenu is
  built by **0x11f3c00**:
  - it lists the MIDI **input devices** and their ports, and ticks the one equal to the selected channel's `+0x2f8`;
  - "No input port set (click to auto assign)" appears for a device with port < −1;
  - "|Assign the first available port number and lock the channel to all midi channels for this device".

  This is a **hardware-input → channel lock**, not a plugin-to-plugin route. [DECOMP]
- **"Always send notes to bus"** exists in FLEngine (0x1469483). [HYP] It is a Patcher / bus option; it was not
  traced. **No "Send note events to"** string exists in FLEngine or the wrapper. [STRINGS]

## Q4 (extra). Do native generators have their own MIDI input and output port?

- **Input.** Every channel object has **`+0x2f8` = locked input port** (−2 = none, judging by the menu code) and
  **`+0x2fc` = MIDI channel filter** (−1 = all).
  - Script `getChannelMidiInPort` (method table at 0x15174e8 → 0xd5bcd0) returns **`channel+0x2f8`** for any channel,
    native or wrapper.
  - "Get receiver port number" (0xd75820, the device-module method) walks the other input devices and returns their
    `+0x30` port.
  - 0x1181620 uses the lock: a **hardware** note from a device with port P goes to every channel with
    `+0x2f8 == P` and `+0x2fc ∈ {midiChan, −1}`, through 0x10002e0. It is therefore played **and recorded**.

  [DECOMP] High confidence. [HYP] The −2 default comes from the `param_4 != −2` test in 0x11f3c00; the default value
  itself was not read.
- **The plugin MIDI-out broadcast (0x132a950) does not use `+0x2f8`.** It calls only each plugin's own
  `MIDIIn(+0x80)`. A native generator therefore receives a MIDI Out / wrapper Output-port word only if its own
  MIDIIn checks the port:
  - the Fruity Wrapper (`+0x854`);
  - Pitcher (p33, round 16);
  - Soundfont Player, 3x Osc and FPC have no port strings. [HYP] They ignore the port byte or the word.

  [DECOMP 0x132a950, 0xfdbe60] High confidence for the routing. Per-plugin MIDIIn was not read for the natives.
- **Output.** Natives have no generic output port. Only plugins that build the port byte themselves send to
  +0x48: MIDI Out (PortSelect, round 16) and the Fruity Wrapper (`+0x858`).

## Address index

- **fw:**
  - 0x7a8620 VST2 host callback; 0x7abb00 the rest of the switch; 0x7b1a00 opcode 8;
  - 0x84d3f0 VST3 output events; 0x85d920 CLAP output event;
  - 0x745810 / 0x746060 TBaseAudioPlugin MIDI out / SysEx;
  - 0x878600 TAudioPluginHost MIDI out (port gate, +0x48); 0x8777d0 SysEx (opcode 0x29);
  - 0x872fd0 TFruityWrapper.MIDIIn; 0x873070 / 0x873100 port setters.
- **FLEngine:**
  - 0x13141b8 TFruityHost VMT; 0x13173e0 Dispatcher; 0x132a950 MIDIOut broadcast; 0x132ab80 immediate variant;
  - slot +0x20 0x132aee0 Voice_Release; +0x28 0x132af00 Voice_Kill; +0x30 0x132af20 Voice_ProcessEvent;
  - +0x100 / +0x108 lock / unlock (FPC);
  - 0xfdbe60 hardware MIDI input; 0xfdb260 / 0xfdb0f0 short message / note;
  - 0x1181620 play live note; 0x1000f10 mapped-channel note; **0x10002e0 record + play**; 0x1060320 / 0x1060460
    recorder note on / off; 0xff4e50 / 0xff5100 play note on / off;
  - 0x11f3c00 "Receive notes from" submenu; 0xd5bcd0 getChannelMidiInPort.
- **FPC:** 0x859470 BtnPadMainMouseDown → 0x870720 → 0x86b480, the internal voice start; 0x8606c2 Dispatcher 0x11
  (send to piano roll).

## Still open

- A runtime check of the FPC pad-click recording claim (see Q2).
- What FLEngine Dispatcher case 0x22 is exactly ([HYP] a MIDI activity / smart-disable wake).
- Which callers use host slot +0x50 (immediate MIDI out) with flag 0. The wrapper's three formats all use +0x48.
- The VST3 legacy-CC conversion (0x84d270) was not read.

## Runtime check (FL 2026.1.7, round 190ks)

- **FPC.** "Notes and automation" was recorded while playing an empty project, with 4 FPC pads clicked with the mouse.
  The FPC channel got no notes. Two typing-keyboard notes played in the same way were recorded. This confirms Q2: FL
  does not record FPC pad clicks.
- **Hosted plugin.** A VST2 test plugin (tools/testplugin/lynx_test_drums_vst2.c, LYNX_TEST_PADS_AUTO) sent note
  on / off on its MIDI output while recording. Its Output port was −1 (FL's default). Its channel got no notes. This is
  consistent with Q1.
