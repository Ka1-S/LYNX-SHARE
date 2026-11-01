# Round 167i (midiout167): issue #17 MIDI Out, issue #18 Channel Rack generator menus

Per item: reporter's words | FL evidence | LYNX before | LYNX now. Evidence markers: [DECOMP] Ghidra on FL's own
binaries (`Plugins/Fruity/Generators/MIDI Out/MIDI Out_x64.dll`, sha256 a87bf4c1…ef45, Delphi, image base 0x400000;
FLEngine `fle.dll` in the read-only project `~/ghproj/r165i`), [DFM] the Delphi forms inside that DLL (RCDATA
`TSYNTHEDITORFORM`, `TCTRLSETFORM`), [TXT] FL's own files (`Data/Patches/Plugin presets/Generators/MIDI Out/*.fst`,
`Data/System/MIDITrack.fst`, `Plugins/Fruity/Generators/MIDI Out/Data/GM *.txt`), [TEST] LYNX unit tests
(`tests/MidiOut167iTests.inc`). Clean-room: only behaviour and data layouts are written here; no FL code or art was
copied. FL Studio was not launched in this round.

## 1. MIDI Out (issue #17, Ivan-Spoda)

- **Reporter:** LYNX's MIDI Out had only channel / port / program; FL's has the patch list, bank MSB / LSB, pages of
  configurable knobs (CC / RPN / NRPN / aftertouch / note), Reset, Deactivate all, "Send pan, volume and pitch",
  "Map note color to MIDI channel", presets; every knob sends when it changes and is automatable. A MIDI file imported
  with MIDI Out channels must bring its controllers like FL.

### 1.1 State (the 0xD5 plugin data) [DECOMP FUN_006fde10 load / FUN_006fe5c0 save] [TXT 41 presets, v1–v5]

- i32 version (FL 2026 writes 7). Header: channel @4, reserved @8 (−1), bank MSB @12, bank LSB @16, page @20, then
  port @24 (version ≥ 2), then two bytes @28 "Send pan, volume and pitch" and @29 "Map note color to MIDI channel"
  (version ≥ 4). A state without those bytes loads with Send pan/vol/pitch on, map off.
- 73 i32: the patch (0 = none, 1…128) and the 72 control values.
- Control records for the non-default controls only: u8 index (0xFF ends), then 4 / 5 / 6 i32 for version < 3 / < 7 /
  7: number (−1 = unassigned; bit 31 set = assigned but deactivated), type (0 CC, 1 RPN, 2 NRPN, 3 channel aftertouch,
  4 note), min, max, channel override (−1 = the MIDI Out's), note-off value (before version 7 = min); then two strings
  (u8 length, 0xFF + u32 for long ones, UTF-8): full name, short name.
- 8 page-name strings, then (version > 5) a u32 with the "On reset" options inverted (a set bit = option off).
  Before version 6 the options follow "Send pan, volume and pitch" (+ cut notes always on).
- Option bits [DECOMP field 0xfe0]: 1 pitch, 2 pitch-bend range, 4 cut playing notes, 8 pan, 0x10 volume, 0x20 program
  change. The bare constructor sets 0x04, but a new instance loads `Default.fst` (version 1) → every option on.
- A new instance gets the next MIDI channel, cycling 0…14 and skipping 9 (the drum channel) [DECOMP].

### 1.2 Messages [DECOMP FUN_006fec60 send control, FUN_006feb10 program, FUN_006feb70 bank]

- CC: `Bn number value`; when the range's max ≥ 128 the value is 14-bit: CC n gets value >> 7, CC n + 32 value & 127.
- RPN / NRPN: CC 101 / 100 (RPN) or CC 99 / 98 (NRPN) = number >> 7, number & 127, then data entry CC 6 (+ CC 38 when
  14-bit). Aftertouch: `Dn value`. Note: the value is the key, the number is the velocity; a new value ends the held
  note and starts the new one; the note-off value only ends it.
- The value is raised to min, then lowered to max. The channel override replaces the MIDI Out's channel.
- Program: `Cn patch−1`, only with the "Send program change" option. Bank: CC 0 / CC 32 when ≥ 0.

### 1.3 Reset [DECOMP FUN_006fd900; callers: FPD_Flush (dispatch 2), FPD_SetEnabled (0xC), the Reset button]

Order: pitch-bend range (RPN 0 = 12: CC 101 0, CC 100 0, CC 6 12), bank, pan (CC 10) / volume (CC 7) / pitch (bend)
— each only when its option is on and a value is known —, the patch, every active control's value (ProcessParam
0x1001 path), then the Note controls' notes off when the generator is disabled and "Cut playing notes" is on. The
editor's Reset button sends CC 121 (Reset all controllers) first [DECOMP FUN_006fec20; DFM ResetBtn hint "Reset all
controllers (CC #121) and everything"]. Options menu [DFM]: "Send all control values now", "On reset": Send program
change, Send pan, Send volume, Send pitch, Send pitch bend range, Cut playing notes.

### 1.4 Parameters and host values [DECOMP FUN_006ffdc0 ProcessParam, FUN_00701900 wheel, FUN_006ffd10 host]

- Parameter 0 = the patch, 1…72 = the controls (automation, pattern events, links). A value from automation or a
  link reactivates a deactivated control. Turning a knob in the editor does the same and sends it.
- The host's channel pan / volume / pitch go out as CC 10 / CC 7 / pitch bend with "Send pan, volume and pitch";
  pitch: 8192 + cents · 8192 / 1200.
- Knob labels [DECOMP FUN_006ff060]: unassigned = a cog, hint "(right click to configure) Unassigned"; assigned =
  short (else full) name, bold when active; a click on the label (de)activates [FUN_006ffa10]; the knob's hint is
  "<name> CC n Chan #n". Deactivate all [FUN_006ffa90]. Page change shows controls page · 9 … +8 [FUN_00701ae0].
  Knob menu: Reset, Configure… (TCtrlSetForm: Type, Full name, Short name, Controller, Range min / max, Channel
  override, Note off; Copy, Paste, Reset, Accept) [DFM].

### 1.5 Presets [TXT]

41 `.fst` files under `Data/Patches/Plugin presets/Generators/MIDI Out` (FLhd + FLdt events 0xC7 version text, 0xC9
"MIDI out", 0xD4 wrapper data, 0xD5 the state above). [TEST] `midi_out_reads_every_fl_preset`: all parse, LYNX's
version-7 writer and its `.fst` keep everything.

### 1.6 "Import MIDI data" with MIDI Out channels [DECOMP fle.dll FUN_00e1d090]

- With the channel type MIDI Out, the new channel loads `Data/System/MIDITrack.fst`: page 1 "Basic controllers"
  knobs 1–6 = CC 1 Mod, 2 Breath, 4 Foot, 5 Port. Time, 91 Reverb, 93 Chorus; page 2 "Boolean controllers" knobs
  10–15 = CC 64 Damper, 65 Porta., 66 Sustenuto, 67 Soft pedal, 68 Legato, 69 Hold 2; all deactivated; patch 1.
- CC 7 → the channel's volume, CC 10 → its pan (value × 100 on FL's 0…12800 scale); CC 1, 2, 4, 5, 91, 93, 64…69 →
  the knobs above (raw 0…127) as pattern events; the other CCs are dropped. Pitch bend → the channel's pitch; program
  change → the patch parameter (data 1 + 1). "Which controller to import" applies only to the piano roll's import.
- [TEST] `midi_out_import_track_state_is_fls_miditrack_fst` compares LYNX's layout with FL's file control by control.

### LYNX before / now

- **Before:** `midictl::parseMidiOut` read channel, banks, port and the patch; a rewrite dropped every control
  record; the renderer sent only bank + program at playback start; the editor had channel / port / program.
- **Now:** `core/MidiOut.{h,cpp}` reads and writes every FL version, builds FL's messages and its reset, and is the
  MIDI Out channel's instrument (`midiout::makeInstrument`): the song's knob values, automation / links on parameters
  0…72 (reactivating), the host pan / volume / pitch, the reset at playback start (FPD_Flush) and on the Reset
  button. Its MIDI reaches the plugins on its port (RackRenderer's control-surface path). "Map note color to MIDI
  channel" routes each note on its colour's channel. `ui/MidiOutEditor.cpp`: FL's layout in LYNX's style (channel,
  patch list with the GM names, bank MSB / LSB, port; page selector + rename; 3 × 3 knobs with cog / labels,
  Configure… window, right-click Configure + automation items; Reset, Deactivate all, the two toggles, Options menu,
  Presets: FL's folder, the user's folder, load / save `.fst`). LYNX extras kept: "Send to a plugin...", "Automate
  MIDI controller...", the "Plays:" list. MIDI import with MIDI Out channels fills FL's lanes (`midiimport`).

## 2. Channel Rack generator menus (issue #18, kubapl2004gmail)

- **Reporter:** Replace showed only Kick…Synth, Sampler and Plugin ›, Insert a longer list: "I want the plug-in
  sheets to be the same for replacement or insertion … no centralized source". And: "I want the drop-down list to
  appear near the item to which it is linked" (0.1.12 opened Replace's list beside the item; 0.1.19+ opened it beside
  the whole two-column menu, so the mouse crossed the right column and opened its submenus on the way).
- **FL evidence [DFM, `FLEngine_x64.dll` file offsets]:** the channel menu's `InsertMenu` ("&Insert", 0x13bf64f) and
  `ReplaceMenu` ("Replace", 0x13bf7ad) hold the same items with the same handlers — "Open plugin picker"
  (`ChanViewPlugPickerMenuClick`), "More plugins..." (`ChanMoreMenuClick`), a separator — and the Channel Rack's
  "Add one" menu (`ChanAddMenu`, 0x13d6bff) and the plugin wrapper's `ReplacePluginMenu` (0x13ce1d3) reuse them; the
  generators are added below at run time from one list, so Insert, Replace and Add one always match. FL's menus are
  `TQuickPopupMenu`s (submenu placement not measured in FL this round; the reporter compares with LYNX 0.1.12). LYNX keeps its own order (built-in block, tools, FL
  engines ›, VFX ›, Patcher, LYNX Soundfont, Sampler…, Plugin ›).
- **LYNX before:** three lists (Insert's `addInstrumentItems`, Replace's `builtInKinds`, the "+" menu);
  `LookAndFeel::preparePopupMenuWindow` (round 162) moved a submenu that overlapped another menu beside the whole
  parent window.
- **LYNX now:** `quickpick::generatorEntries` is the one list (with section / kind / availability) and
  `quickpick::addGeneratorMenu` builds Insert ›, Replace › (the channel's own generator ticked) and "+" from it;
  Replace keeps the channel's place, name and mixer insert (`replaceGenerator`). The submenu relocation is gone:
  JUCE opens a submenu right beside its item and flips it only when it does not fit the screen.

## Verification

- [TEST] core suite with FL's data folder: 1,889,373 checks, 0 failures (new: `tests/MidiOut167iTests.inc`, 7 tests;
  `Feat133Tests` now expects FL's reset at playback start).
- [GUI, Windows, real mouse, 1920×1080, fresh profile, LYNX_TEST_MODE] screenshots in
  `E:\lynx-work\midiout167\s\`: `a3` Replace › opens beside its item with the full list, Kick ticked; `a4` Kick
  replaced by MIDI Out, its editor; `a5`–`a6` FL's 41 presets, "Nord Lead 2" loaded (pages, names, ranges);
  `a7`–`a8` knob menu (Configure..., automation / remote items) and the Configure window; `a9` a label click
  activates (bold), Options menu with FL's "On reset" ticks; `b1`–`b2` "+ add channel" = the same list, FL engines ›
  beside its item; `b5`–`b7` Channel Rack at the screen's bottom-right corner: Replace › moves up to stay on screen,
  still beside the item, and a click on an item that overlaps the parent menu works (Layer created).

## Where I stopped

- Done: MIDI Out core + editor + import, one generator list, submenu placement; tests and Windows build green.
- Left: the Patcher's "Add" menu and the quick-add palette keep their own generator lists (Patcher adds the
  Keyboard Controller, the palette searches); not part of issue #18's Channel Rack menus.
