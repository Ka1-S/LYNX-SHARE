# REA round 190qs: FL's trial mark on plugin records (issue #282), FL generators after Export to FL Studio

Static only, clean-room, facts in my own words. Binary: FL Studio 2026 `FLEngine_x64.dll` (the analysed Ghidra project
of round flgap187a, headless, read-only). Addresses are FLEngine's. Raw pseudocode:
`/mnt/e/lynx-work/issue282_190qs/rea/d1…d3`. Earlier facts used: `REA_rodada190gy.md` §1 (trial-saved projects),
`REA_rodada190i.md` §1 (the plugin record's flags), `REA_rodada5.md` §1.1 (the record).

## 1. The report (issue #282, FL Studio 2026.1.7 trial, Windows 10)

A project made in FL's trial with Slicex, SoundFont Player and 3x Osc opened in LYNX; after Export to FL Studio (.flp or
the zip) FL's Channel rack showed only 3x Osc. The attached minimal case ("Slicex Test.zip") is LYNX's own zip export
of a one-Slicex project (four 808 Samplers, Slicex "boobassguitarchromatic" with its 23.6 MB sample inside its state,
Emphasizer on the Master) **[DATA]**:

- Its Slicex channel record is the one FL writes (0x40, 0x15 = 2, 0xC9 "Slicex", 0xD4, 0xCB, 0xD5, ...), and
  LYNX exporting it again gives the same bytes for that channel. The 0xD5 state starts as FL's Slicex state does.
- Its 0xD4 flags (u32 at +16) are **0x1d0** = 0x150 (FL 2026's usual generator value) **+ 0x80**. LYNX keeps an
  imported channel's flags (`ChannelDef::flWrapperFlags`), so 0x1d0 is what the user's trial wrote.
- None of the 7,057 plugin records in FL's 165 shipped projects (`Data/`) has 0x80. The trial-saved project of issue
  #171 has it on its Emphasizer (0x1c0) and not on FLEX (0x150; FLEX is free in every edition).

## 2. What 0x80 means **[STATIC]**

- **Saving** (FUN_00f3c870, the plugin part of the channel / slot saver): the record's flags start from the plugin's
  window flags (& 0x30d), then +0x10 for a generator, +0x20 Smart disable, +0x40 threaded. For a plugin (kind 2) it
  reads the plugin's info flags (FUN_01330870: the plugin's info record, u32 at +0x14) and, when bit **0x8000** is
  set there and a writer option (+0x4698 bit 4) is off, adds **0x80**. Bit 0x8000 of a plugin's info flags is the FL
  SDK's "demo version" flag: the plugin runs as a trial. The record is then written as event 0xD4 (52 bytes), the
  state as 0xD5.
- **Loading** (FUN_00f46490, case 0xD4): when the record has 0x80 and the newly made plugin is a kind-2 plugin whose
  info flags have 0x8000 (still a trial in this FL), the plugin is put on a list instead of being set up, and the
  following 0xD5 is **not given to it** (the 0xD5 case skips plugins on that list). At the end of the load the list goes
  to FUN_00c31890, which shows FL's notice: "The plugin is a limited trial version and will not load these settings
  until you buy it." / "One plugin used in this project is a trial version. It will now be locked in this project. You
  can unlock it by buying the plugin." with "NOTE: Do not resave locked projects. The locked plugin(s) will be lost."
  and a "Buy plugin" button.
- An FL in which the plugin is not a trial (registered, plugin owned) takes the normal path and loads the state.

So 0x80 is FL's own trial lock of one plugin's settings. In the user's FL trial, Slicex (record 0x1d0 in the file) and,
by the same path, SoundFont Player are trials; 3x Osc is part of every FL edition and is not, which fits only 3x Osc
surviving. The same lock applies to FL's own save of that
project (and an unregistered FL refuses trial-saved projects altogether, REA_rodada190gy §1).

## 3. LYNX

- Before: an imported channel's flags went back unchanged (0x80 included), also when the channel had become another
  instrument in LYNX; mixer effects always got fresh flags (0x140 / 0x160), so an effect FL had locked went back
  without its lock.
- Now (`core/FlWrapper.h` `flTrialSaved`, `FlOrigin::trialSaved`, `core/FlpExport.cpp` `keepsTrialMark`): the mark
  stays with the plugin it was saved with — the channel's generator or the slot's effect, edited in LYNX or not, as
  FL's own resave keeps it — and is saved in `.lynx` (`flOrigin.trial`). A channel that holds another generator, a
  Sampler or a hosted plugin now loses it (FL only writes it on plugin records it saved as trials). LYNX never removes
  the mark from FL's own plugin: that would unlock FL's trial. The export report says, per plugin: "... was saved by a
  trial of FL Studio. Where ... is still a trial, FL Studio keeps it locked and does not load its settings (as with
  FL's own trial saves); an FL Studio that owns it loads them."

## 4. FL's generators after a LYNX round trip **[DATA]**

FL's 30 one-generator render tests (`Data/System/Render tests`: 3OSC, Autogun, Bassdrum, Beepmap, Boobass, Directwave,
Drumaxx, Drumpad, Drumsynth Live, FL Keys, FLEX, FPC, Fruit Kick, Fruity DX10, Fruity Granulizer, Fruity Slicer, Fruity
Voltage Controller, GMS, Harmless, Harmor, MiniSynth, Ogun, Patcher (generator), PoiZone, Sakura, Sawer, SimSynth,
Slicex, Sytrus, Wave Traveller) imported by LYNX and exported: every generator goes out under FL's internal name, type
2, FL's flags and **byte-identical** state, except **Patcher (generator)**: LYNX plays a Patcher channel through its main
generator and wrote that one back (here "Sytrus" with an empty state: a default Sytrus in FL). Now the channel's FL origin
is FL's Patcher with its whole state, so it goes back as FL's Patcher. Test
`issue282_fl_generators_go_back_as_themselves`; FL's renders of both files: HANDOFF of round 190qs.
