# REA round 190gy: trial-saved .flp values, FL's Stop / seek mark (facts)

Static only, clean-room, facts in my own words. Binary: FL Studio 2026 `FLEngine_x64.dll` (the analysed Ghidra
project of round flgap187a, headless, read-only; objdump listing of round 167j). Addresses are FLEngine's.
Raw pseudocode: `/mnt/e/lynx-work/bugs190gy/rea/d1…d11`.

## 1. Projects saved by an unregistered FL (issue #171)

- The project saver (FUN_00f40840; the channel saver FUN_00f3e190 and the mixer track saver FUN_00f3d620 the same)
  writes event 0x1C with 1 when FL is registered and with 0 otherwise. Right after writing 0x1C = 0 it sets a flag on
  its writer object (+0x11).
- With that flag set, every fixed-size value it writes (u8, u16, u32 events) first goes through FUN_00a9ece0: byte j
  of the value is XORed with `0x79 − (stream position of the value's first byte) + 3·j`, all mod 256. The position is
  the file offset (FUN_005123a0 = the stream's position). The event ID (and the 0xAC escape with its 32-bit ID) is
  written plainly by FUN_00a9ec60; data events go through FUN_00a9ea80 with their varint size and payload untouched.
  So text (names, sample paths) stays readable while tempo, channel numbers, types, routing, etc. look like noise.
- The loader (FUN_00f46490): event 199 (0xC7, the version text) sets the global file version DAT_016bac50 as
  major·1,000,000 + minor·1,000 + patch (and the UTF-16 flag for text when it is above 11,005,004). Event 0x1C calls
  FUN_00f46420 with the value: if the value is 0 and the file version is at least 20,099,001 (FL 20.99.1 = FL 21's
  betas), the reader's flag +0x11 is set; if FL itself is unregistered it also stops loading (+0x4448 = 1: the trial
  can't open trial saves). Its value readers FUN_00a9e430 / 4d0 / 570 (u8 / u16 / u32) undo the XOR with the same
  key when the flag is set.
- Files older than 20.99.1 with 0x1C = 0 (FL 20 demo saves) hold plain values.
- The report's project (FL Studio 25.2.5.5319, 0x1C = 0): LYNX read 252,572 BPM, channel numbers like 60906 and
  mixer routes like 17472 before; with the key undone: 130 BPM, channels 0–4, inserts 1–5, the 808 sample paths
  `%FLStudioFactoryData%\Data\Patches\Packs\Legacy\Drums\Dance\Basic 808 *.wav`, FLEX on channel 5.
- LYNX: `flp::EventReader` tracks the 0xC7 version and turns the same XOR on after 0x1C = 0 (version ≥ 20.99.1);
  `isScrambled()` reports it. Every reader of FL files (projects, .fst states, scores) goes through it.

## 2. Stop, Space, pause and "Remember seek time" (issue #169)

- "Remember seek time" is FL's Stop button menu item RemSeekTimeMenu (DFM TQuickPopupMenu StopMenu). It is saved as
  `RemSeekTime` (registered at 0x119ff5a with the variable at 0x15d89a0). The variable starts as **1** in the data
  section, so a fresh FL has it on.
- FUN_011c9300 (pos, flag) is FL's seek mark (global 0x15e0458): with the option off it is always −1; with it on, a
  new position is stored. FL's "set song position" (FUN_011e7a10) calls it, so every position the user sets (ruler
  clicks in the Playlist or the Piano roll, also while playing) becomes the mark.
- FUN_011ca520 (state, flags) is the transport: 1 = play, 0 = stop, 2 = pause. The toolbar's Play handler
  (FUN_01030360) passes 1 when the button goes down and 2 when it comes up while playing: Play pressed while playing
  is a pause. StopBtnClick (0x1030470) passes 0. Stop and pause both run the stop routine FUN_011ca290; only Stop (0)
  posts the extra message 0x56e/7 that sends the position back, and Stop while already stopped runs FUN_011c9de0: the
  position to the start, the mark cleared when it was there.
- Switching Pattern / Song (FUN_011ca630) clears the mark.
- FL sessions measured earlier with the option on (this install's registry RemSeekTime = 1): Pattern mode, Piano
  roll ruler at 2:09, Play, Stop → 2:09:01, Stop again → 1:01:00 (REA_rodada169f); Song mode, ruler at bar 5, Play,
  Stop → 5:01, Stop again → 1:01 (REA_rodada165z-a 9.10); Space plays from the click and stops back there, also when
  the ruler was clicked during playback (REA_rodada165-issue11c C3, Q2).
- LYNX before: Space went back to where playback started (a click during playback was ignored); the Stop button went
  to 1:1:1 unless "Remember seek time" was on (off by default) and only in Song mode, with the mark set only by the
  Playlist ruler. Pause (Ctrl+Space, the Piano roll's play button) kept the position, as FL's pause does.
- LYNX now (`transportedit::stopReturnTarget`, `MainComponent::stopReturn`): "Remember seek time" on by default
  (saved under a new key, so installs that saved the old default get FL's); the mark is the last Playlist or Piano
  roll ruler click (also during playback) or else where playback started, per Pattern / Song mode; Stop, Space and
  the Piano roll play button's right-click go back to it; Stop while stopped goes to the start and forgets it; pause
  and resume keep it. With the option off: Space back to where playback started, Stop to the start (as before).
