# Round 190hz — DirectWave's editor, its state as a document, .dwp / .dwb files (issue #157)

> Clean-room, behavioural. **[DEC]** decompiled / disassembled code of FL's own DirectWave (`Plugins/Fruity/Generators/
> DirectWave/DirectWave_x64.dll`, the analysed Ghidra project of round 72; `editor_x64.dll`, the Delphi editor, read
> with objdump and its form / method / field tables), **[FORM]** the editor's form resource (round 188c's dump
> `slicex188c/rea/dfm-dw-sum.txt`, `dfm-dw-menus.txt`), **[EXEC]** FL 2026 itself (live session 2026-11-15
> 16:20–16:28 under the GUI + FL locks, our own copy of `System/Render tests/Directwave.flp`, FL's saves of it),
> **[DATA]** the 57 DirectWave states of FL's demo projects and FL's shipped `.dwp`. Facts in our own words; no FL code,
> no FL bitmaps, no FL samples in the repository. Work folder: /mnt/e/lynx-work/dwave190hz (rea/ decompiles, fl/ the FL
> session and its shots, p/ scripts).

## 1. The editor (FORM + EXEC)

The form is 990 × 627 (`TSynthEditorForm`). FL's shot (`fl/shots/s00-c.png`, `z01-c.png`, `s01-c.png`, `o01-c.png`):

- **Top**: the disk-operations button (the Options menu), ONLINE CONTENT, help, the 16 MIDI channels, then the tab bar
  MULTI BANK | PROGRAM (index, ◀ ▶, ▾ program list, name) | LIBRARY (◀ ▶ ▾, name).
- **Zone grid** (`ZoneGrid`, 709 × 125): columns, from the editor's string table in this order: Zone, Zone Name, Size,
  Rate, Mute, Root, Semi, Fine, K-Trk, Ticks, SY, SL, L-Key, H-Key, L-Vel, H-Vel, Lk, VC. FL shows the zone number with
  three digits, the keys and the root as numbers (36, not C3), Size in frames, SY / SL / Lk as switches.
- **Zone map** (`ZoneEditor`) under a splitter: the keys across, the velocity down (000 at the top, 127 at the bottom,
  the scale on both sides), each zone a box over its keys and velocities with its name upwards in lower case; the
  current zone filled. The keyboard (`MIDIKb`, 675 px) spans the same keys; the ↕ button grows the map.
- **Bottom pages** (`BandSel`, top to bottom): SAMPLE, ZONE, PROGRAM; FL opens on PROGRAM.
  - PROGRAM: MASTER (play mode Poly / Mono / Legato, Volume), GLIDE (time, Fixed / Auto), FX DRIVE A / B (amount and a
    switch in the header), LFO 1 / 2 (rate with tempo sync, phase, attack, waveform), FX DELAY (delay, feedback, low cut,
    high cut, Bounce / Normal), FX REVERB (room, damp, diffusion, decay), FX CHORUS (delay, depth, rate, feedback).
  - ZONE: MAIN | TIME | TRIGGER (pitch tune, gain, panning, post gain / time, grain, smooth with the mode and sync
    switches / trigger group, type, frequency, overlap), FILTER 1 | 2 (cutoff, resonance, shape, type, serial /
    parallel routing), AMP | ENV 1 | ENV 2 (A D S R sliders), LFO 1 | 2, MOD MATRIX pages 1–4 (source, destination,
    amount per slot), and the row RINGMOD, DECIMATOR, QUANTIZER, PHASER (each with a switch), SEND FX (delay, chorus,
    reverb), EDIT / AUTOMATION (Current selection / Global).
  - SAMPLE: the wave with its Start and Loop tags, the overview, the path, START, LOOP START, LOOP END, LOOP TYPE.
- **Side panel**: FOLDERS (path, home, PARENT, BROWSE, undo, preview volume, PREVIEW, the file list, "n File(s)
  Selected") | OPTIONS (content library; Global: import to content library, multitimbral, tablet, switch program
  channel, save wav type; Bank: 16 point sinc, monolithic mode, high quality rendering, note names in host; Samples:
  pitch analysis, auto normalize, root key from file name, load progress; Compatibility: compatible velocity, clip
  volume).
- **Menus**: the zone grid's right click (New zone; Manage zones: Delete zone(s), Delete all zones, Duplicate zone;
  Samples: Add sample(s)..., Replace sample(s)...; Mapping: Automap zone(s), Remap zone(s) as drumkit; Copy: Copy to all
  zones ▸, Copy to selected zones ▸ (greyed with one zone selected); Locking; Misc: Find pitch root...) and the disk
  button's menu (Open program / bank..., Create program from samples...; Program: Save, Save as..., Save as
  variation..., Save for FL Studio Mobile...; Bank: Save, Save as...; Program: Flush, Flush all; About). Shots
  `zm01-m.png`, `dm01-m.png`. The menu items' tags are the values they store (filter types, loop modes, waveforms,
  play modes, trigger types, the 20 modulation sources and 48 destinations).

## 2. The state (DATA + DEC)

Layout as REA_rodada9 §2, with these additions:

- FL 2026 writes **version 38** (0x180040750 writes 0x26) and a 7-byte 0x206 chunk (0x180040610: four option fields and
  the save flag); FL's plugin database preset (a new DirectWave) is version 37 with a 5-byte 0x206.
- Flags chunk 6 (0x180040750): bit 0 = Clip volume, bit 1 = Compatible velocity, bit 2 = a third option, bit 3 =
  "not registered" (then nothing but the flags is written), bits 4 / 5 = monolithic mode.
- MIDI slot chunk 5 (628 bytes, 0x180040860): u32 slot, u32 (60), 512 bytes of per-note data, then **byte 520 = the
  slot's program**, byte 532 (200), f32 at 572 / 576 / 580 = volume 100/127, pan 0.5, expression 1.
- Program 0x64: bytes 14–17 = **the number of zones** (7296 programs, no exception).
- Zone header 0x1F4: byte 6 = **lock** (Lock / Unlock all zones write it, 0x180018c17 commands 16608 / 16609; locked
  zones are skipped by Copy to …); byte 7 is not written by FL's writer (stack bytes: 0 / 64 / 255 in the demos);
  the editor's selection is a separate byte of the zone object (+0x2bb), not saved.
- The 57 demo states are made of exactly these chunks in a fixed order per zone (0x205 optional), no leftover bytes.

**A new zone** (DEC: 0x180035de0 adds one, 0x180035870 resets it; 0x18001ddb0 loads its sample; EXEC: FL's save after
Add sample(s)...): root 60, keys 0–127, velocities 0–127, gain 1, pan 0.5, pitch 0.5, key tracking 100, ticks 16,
rate 44100 before the file is read, loop Disabled, sends 0.5 / 0.5 / 0.5 and dry 1, both filters off at cutoff
(log2(1)+5)/10.583333 with resonance 0.5, amp envelope 0 / 0.5 / 1 / 0.25, both mod envelopes 0 / 0.5 / 0.5 / 0.25, LFO
1 rate 0.1, LFO 2 rate 0.55 (phase 1 = free run), effect mixes 1 and phaser 0.5, matrix slots Note Velocity → Voice
Gain (1.0), Mod Wheel → Mod Amt P1-3 (0.75), Zone Lfo 2 → Voice Pitch (0.5), the rest none (0.5). The name is
"<New Zone nnn>" for New zone, the file name without its extension for a sample (0x180003c70: cut at the last '.',
then after the last '\' or '/'). The sample sets frames, channels, rate, bits (16 for 8/16-bit PCM WAV, else 32), and
**ticks = 4 × the length in half seconds rounded to the nearest power of two** (at least 1; ties go up). A WAV's 'smpl'
chunk sets the root (unity note) and the first loop (Forward, A = start, B = end + 1); its 'inst' chunk sets root, fine
tune, key and velocity range. The first sample names a program called "---". LYNX's zone record for our tone is FL's
912 bytes exactly (test `directwave_new_zone_is_fl_new_zone_byte_for_byte`).

## 3. The zone menu (DEC: the command handler 0x180018c17)

| tag | entry | what it does |
|---|---|---|
| 16600 | New zone | a reset zone at the end, named "<New Zone nnn>" (nnn = the count before), the only one selected |
| 16601 | Delete zone(s) | removes the selected zones |
| 16602 | Delete all zones | |
| 16603 | Duplicate zone | a copy of the current zone and its audio at the end |
| 16604 / 16605 | Add / Replace sample(s)... | 0x18001d4a0: Add = one new zone per file; Replace = the files into the zones from the given one on, keeping their key / velocity ranges and root (and key tracking for one file) |
| 16606 | Automap zone(s) | 0x180013d50: each zone with audio spans from its root to below the next higher root of any zone (127 at the top); then the zones whose low key is the lowest of all zones get 0 |
| 16607 | Remap zone(s) as drumkit | zone i's root = (60 + i) mod 128, then Automap |
| 16608 / 16609 | Lock / Unlock all zones | header byte 6 = 1 / 0 |
| 16700–16712 | Copy to all / selected zones ▸ | from the first selected zone into every other unlocked zone (all, or the selected ones): key tracking, fine, ticks, transpose, beat sync, wave slice, mute group, sample rate, root, low key, high key, low velocity, high velocity |
| 16650 / 16651 | Flush / Flush all | 0x18002c0d0 on the program / all 128 |

Bank file (.dwb, 0x180014020): "DwBk" + the whole state. Program file (.dwp, 0x180014140): "DwPr" + the version and
flags chunk + the program record (with "Save for FL Studio Mobile" the samples are first copied next to the file).

## 4. Value texts (DEC 0x180027740 and helpers)

The labels under the names show the plugin's parameter text; the parameter numbers are the form's wheel tags.
Gains (master volume, zone gain, post gain, sends): `20·log10(v²)` "%.1f dB" ("-oo dB" at 0). Times (glide, LFO
attack, envelope A / D / R): `10·v⁴` "%.3f s". LFO rate: "%.3f Hz" of `20·v²`, or with tempo sync "%d Ticks" of
`int(127.5·v² + 1)`. Phase: "%.1f" of `360·v`, "Free Run" at 1. Pan: "Center", "+NN R", "-NN L" of `int(100·v) − 50`.
Cutoffs (filters, delay low / high cut, ring modulator rate, phaser frequency): `2^((127·v)/12 − 5)·440` "%.1f Hz".
Pitch: "%d Cents" of `(v − 0.5)·2400`. Mod amounts: "%.1f%%" of `200·v − 100`. Delay: "%.2f" (beats); reverb damp
`21.5·v + 0.5` kHz, decay `19·v² + 0.1` sec; chorus delay `47.9·v² + 0.1` ms, depth `24·v²` ms, rate `20·v²` Hz;
stretch time "%d Ticks" with sync, else `2^((v − 0.5)·8)·100` %; decimator step / bit reducer bits "%.3f"; sample
points in frames; the rest "%.2f%%" or "%.1f%%". (The trigger frequency / overlap wheels are not plugin parameters:
LYNX shows a percentage — approximate.)

## 5. Edit mode (EXEC)

FL opens with **Global** on: a zone wheel then sets every zone (FL's save after one pitch drag: all 8 zones 0.5195).
With "Current selection" on and one zone selected, FL's pitch wheel did not move in two drags (`fl/shots/cs03`,
`cs04`) — **needs a live check**. LYNX: Global = every zone, Current selection = the selected zones.

## 6. LYNX

`src/core/DirectWaveState.{h,cpp}`: the state as a chunk tree (read / write byte for byte; the 57 demo states and
programs rewritten unchanged), `setProgram` writes only the changed fields and keeps every byte it does not know,
zones keep their records by origin (new zones get FL's record), the zone count follows; FL's new DirectWave
(`freshDoc`, byte for byte the plugin database preset), FL's new zone, .dwp / .dwb read and write, the zone-menu
actions, the WAV chunks, and an SFZ → program mapping (LYNX's own, see §7). `dw::playedProgram` now follows MIDI slot
0's program (FL's channel 1) when it has zones, else the first program with zones as before (every demo: program 0).

`src/ui/DirectWaveEditor.{h,cpp}`: FL's layout at 1.25 × (positions from the form), the program bar, the zone grid with
FL's columns (double click edits a cell; SY / SL / Lk switch on a click), the zone map (drag a box or its edges), the
keyboard, the three pages with FL's groups and value texts, FOLDERS (browse, preview, double click or right click
Add / Replace sample(s), .dwp / .dwb / .sfz open) and OPTIONS (Compatible velocity, Clip volume), the disk menu (Open
program / bank, Program Save / Save as, Bank Save as, Flush, Flush all), drop of files on the editor. DirectWave is in
"add channel" (a new one = FL's new DirectWave). `LYNX --dwedittest project|new script report [prefix]` drives the
editor off screen.

## 7. Left out / approximate

- Left out (no dead controls): MULTI BANK and LIBRARY tabs, the MIDI channel selector (LYNX plays MIDI channel 1),
  ONLINE CONTENT / help, Create program from samples, Save as variation, Save for FL Studio Mobile, the global and
  bank options LYNX does not perform (sinc interpolation, monolithic mode, HQ rendering, note names, pitch analysis,
  auto normalize, root key from file name, multitimbral, tablet, save wav type), Find pitch root, the Sample page's
  editing tools (TMEnvEditor buttons), the peak meter's level, .sf2 / .gig / .exs / .nki import (FL's own converters;
  their samples live inside the files).
- Approximate: the SFZ import (LYNX's mapping of the opcodes, not FL's converter); the trigger frequency / overlap
  value texts; LYNX's look (its theme, no FL bitmaps).
- Needs a live check: "Current selection" edit mode (§5); the zone map's mouse gestures (LYNX: drag a box to move its
  keys, its edges to change keys / velocities — FL's map gestures were not exercised in the session).

## 8. Best way?

- **FL:** as above (DEC / EXEC per item).
- **Ableton:** Sampler's Zone editor shows key, velocity and sample-select zones as bars over a keyboard with crossfade
  handles, edited by dragging; Simpler has no zones (/mnt/e/lynx-work/ableton/STUDY.md). Live's map has crossfades
  DirectWave lacks.
- **Users:** issue #157 asks for the editor, .dwp / bank saving and SoundFont / GigaSampler / Kontakt / SFZ import; no
  web search was made this round.
- **Verdict:** FL-exact layout and behaviour as the default. Proposal (not built): velocity crossfades in the map, as
  an option, if the coordinator wants them.
