# REA round 168q: designed editors and LYNX banks, group D (v0.1.23)

Group D of the design push (docs/analise/REA_rodada168n.md §1.3): **Kepler, Kepler Exo, Transistor Bass, Sawer,
BeepMap, Fruity Granulizer, Wasp / Wasp XT / Fruity Dance**. Each engine kept the plain grid of knobs (or no page at
all); this round gives each one FL's arrangement drawn in LYNX's style, an automatable parameter table
(core/ModelParams.h, round 168n's shared infrastructure) and, where the engine plays without FL Studio's files, a LYNX
preset bank (core/LynxBanks.h).

Clean-room: the layouts below are read from the plugins' own editor forms (Delphi DFM resources inside FL's own
plugin DLLs, REA on FL's binaries only). Nothing of FL's art is copied; FL's presets are not converted.

## 1. FL evidence: the editor forms

Every FL plugin DLL of this group keeps its editor as a `TSynthEditorForm` DFM in RCDATA (`TPF0` stream). The forms
were dumped with our own reader (work dir `~/.claude/jobs/f8789b49/tmp/r168q`: `dfmall.py`, `compact.py`,
`dfmctl.py` → `*_x64.dfm.txt`, `*_x64.c.txt`). The `SynthPanel` child gives the panel size; each wheel / checkbox /
combo carries its parameter number in `Tag`, its range in `Min` / `Max` and its default in `DefaultValue`.

| Plugin | DLL | Panel | Parameters on the panel |
|---|---|---|---|
| Kepler | `Kepler_x64.dll` | 1080 × 217 | `Tag` = "Kepler-1" panel float 0…35 (0…1); ARP, LFO, DCO, HPF, VCF, VCA, ENV, CHORUS strip, Hold / Octave / Volume |
| Kepler Exo | `Kepler Exo_x64.dll` | 1080 × 574 | `Tag` = internal index (155 values 0x00…0x9a; 0x54 bend range and 0x5b hidden are not on the panel); effect pages Sat / Chorus / EQ / Delay / Reverb |
| Transistor Bass | `Transistor Bass_x64.dll` | 700 × 600 | `Tag` 0…41 = the current program's working patch (0…1); the 16-step sequencer below |
| Sawer | `Sawer_x64.dll` | 897 × 622 | `Tag` 0…95: floats 0…1, 74 arp division 0…6, 75 polyphony 0…5, 76 bend 0…11, 84–89 matrix destination −1…72, 90–95 matrix source 0…131 |
| BeepMap | `BeepMap_x64.dll` | 380 × 170 | Freq (0: 24…9600, def 7200), Length (1: 0…4096, def 2048), Scale combo Log / Linear / Harmonics (2), Use blue / Grainy / Loop / Widen (3…6), Max height (digit wheel 3…999, def 23), Open / preset / Copy / Paste, the bitmap |
| Fruity Granulizer | `Fruity Granulizer_x64.dll` | 392 × 429 | GRAINS: attack 50…500, hold 0…500, grain spacing 125…1000, wave spacing −1000…1000; EFFECTS: pan 0…64, depth 0…384, speed 4…200, rand 0…256; TRANSIENTS: hold 0…500 + source selector; TIME: start 0…4096, Loop, Hold, Key to (Pitch / Percent / Step / Transient), Tempo locked; the waveform |
| Wasp / Wasp XT | `Wasp_x64.dll` | 356 × 201 | FL's "Dummy x64" form: the name and "This plugin doesn't have a 64 bit version." / "Your settings have been saved." / "Open this project in FL Studio 32 bit to use this plugin." |

Switch values (from the engines' own decoding, earlier rounds): Kepler arp mode 0 / .5 / 1, range 1 << (2x + .5),
PW source LFO / Manual / Env, chorus (int)(3x + .5) four modes, octave (int)(2x + .5) − 1, VCA env when x < .5. Kepler
Exo shapes x·5 (Sine / Tri / Pulse / Saw / Noise / DCO), LFO 2 shapes Tri / Ramp up / Ramp down / Sine / S&H / Random
glide, arp mode x·8 (Up, Up/Down, Down, Random 1–3, As played up / up-down / down), VCA source < .333 Env 1, middle
Gate, high Env 2, filter slope 6 / 12 / 18 / 24 dB. Kepler Exo's mod-matrix source numbers do not follow the DLL's
string list, so only the eight amounts (0x5c…0x63) are on LYNX's page. Sawer's matrix source names come from
`Sawer_x64.dll` 0x321034+ ("---", Velocity, Aftertouch, PitchWheel, CC n); arp modes Up / Down / Up-down / Random; LFO
shapes Tri / Saw / Square / S&H.

## 2. BeepMap's sound, as the LYNX bank uses it

From the engine (core/BeepMap.cpp, REA_rodada65 §2): row r of an H-row image is one sine oscillator; a pixel's red is
its left level, green its right level, scaled by 1 / (255·√H). Columns last
(e^(Length/4096·ln 33) − 1)·256 + 64 samples at 44.1 kHz and the levels ramp across each column.

- **Harmonics**: row r sounds (H − r) × the note two octaves down (so the played note is harmonic 4; organ drawbars
  16', 5⅓', 8', 4', 2⅔', 2', 1⅗', 1⅓', 1' are harmonics 2, 6, 4, 8, 12, 16, 20, 24, 32), rolled off above
  (11^(Freq/9600) − 1)·1102.5 Hz over 480 Hz.
- **Log**: row offsets (H − 1 − 2r)·Freq / (2(H − 1)) cents: with Freq = (H − 1)·100 the rows are semitones and the middle
  row is the played note.
- **Linear**: rows (H − 1 − 2r)·(3·Freq / 2) / (2(H − 1)) Hz around the note.
- Loop restarts the image (with one column: a held tone whose attack is the column); without Loop the image is the
  envelope and the voice ends after the last column. Loading a new bitmap kills the voices (FL, FUN_0072f780); moving a
  knob does not.

## 3. LYNX now

### 3.1 Pages (core/EngineLayouts.cpp)

| Engine | Page |
|---|---|
| Kepler | the eight sections in FL's order and widths, faders and switches at FL's positions, the 36 panel parameters |
| Kepler Exo | top bar (X / Y, detune, volume, pan), the oscillator / filter / envelope / LFO / arp sections, the effect pages as page tabs |
| Transistor Bass | voice, tweaks, distortion, delay, reverb as FL; the program selector and the 16-step sequence as displays |
| Sawer | FL's 897-wide panel, the arp / polyphony / bend menus and the six-slot modulation matrix with FL's source and destination names |
| BeepMap | Freq / Length wheels, Scale menu, the four switches, the bitmap row (open / paste, rows stepper) and the bitmap |
| Fruity Granulizer | GRAINS, EFFECTS, TRANSIENTS (source as a radio), TIME (Loop, Hold, Tempo locked, Key to menu), the waveform |
| Wasp / Wasp XT | FL's placeholder message (no 64-bit version, settings kept, open in 32-bit FL) |

### 3.2 Parameter tables

`keplerParams()` (36), `keplerExoParams()` (the panel's indices), `transistorBassParams()` (42), `sawerParams()` (94),
`beepMapEngineParams()` (8: settings 0…6 and 14; 7…13 are unused by the engine), `granulizerEngineParams()` (15: the 12
knobs, then 12 transients source, 13 key to, 14 tempo locked). Each engine applies a moved knob without rebuilding (the
notes go on): Kepler / Sawer / Transistor Bass through a LiveParamQueue drained on the audio thread, BeepMap keeps the
decoded bitmap when only the settings changed, the Granulizer's setup swap never resets voices. Automation clips reach
them through `Instrument::setParameter` (BeepMap's Max height and the Granulizer's source / tempo lock follow the
channel state only: they re-shape the bitmap / re-slice the sample).

### 3.3 Banks

- **BeepMap** (core/BeepMapBank.cpp, 21 presets, drawn by code): Organ (Jazz Drawbars, Gospel Full, Stops Out, Soft
  Flute Stop, Reed Organ, Click Organ), Lead (Additive Saw / Square), Bass (Sub Bass, Pluck Bass), Keys (Music Box,
  Tubular Bell, Glass Keys), Pad (Breathing Pad, Beating Choir), Arp (Major Arp, Minor Arp), Chord (Minor Stab, Seventh
  Pad), FX (Rising Sweep, Grain Sparkle). Each column is scaled so its levels sum to at most 0.55 (the oscillators'
  worst case in phase): no preset clips on its own. Probe (one held note, 44.1 kHz, keys 36 / 60 / 84): peaks
  0.09…0.38, RMS 0.012 (the short plucks and bells over 3 s) … 0.125; every state usable.
- **Fruity Granulizer** (core/GranulizerBank.cpp, 12 presets): settings over the channel's own sample (Init Grains,
  Smooth Stretch, Half Time, Double Time, Frozen Pad, Shimmer Cloud, Tape Wobble, Backwards, Stutter Glitch, Grain
  Spray, Key Scrub, Slice Player); every preset starts from the plugin's defaults and keeps the channel snapshot.
- Kepler, Kepler Exo, Transistor Bass and Sawer run FL's own engine DLL / data file (`samplePath`): no LYNX bank; the
  pickers already say "(requires FL Studio)" without an installation, and the page says which file is missing.

### 3.4 Tests

tests/Design168qTests.inc: tables sorted / unique / registered, every value control of each page names a parameter of
its engine and lies inside the page, BeepMap / Granulizer state read and write, automation through the Song, a BeepMap
knob moved under a playing note keeps the voice, both banks render clean (finite, peak < 1, RMS > 0.002 for one note
and a three-note chord).

## Where I stopped

- **Done** (branch `feature/design168q`, pushed as `wip/design168q`): layouts for all seven pages; parameter tables and
  live / automation paths for Kepler, Kepler Exo, Sawer, Transistor Bass, BeepMap, Granulizer; BeepMap and Granulizer
  banks; round 168n's wip infrastructure merged in (commit 6055913f) and everything registered on it
  (ModelParamList.inc, LynxBankList.inc, instrumentLayout(), build lists, tests.cpp); ModelPanel displays (BeepMap
  bitmap + Open / Paste row, Granulizer waveform + grain envelope, Transistor Bass program + sequence), the "Needs FL
  Studio's files" header for Kepler / Kepler Exo / Transistor Bass / Sawer without their engine file; translations.
- **2026-11-02 crash (~11:10)**: worktree checked clean (no zero-length files, HEAD = wip b4fcda94); merged origin/main
  efb8344d and the latest wip/design168n (7a9f3cee); builds re-queued.
- **In progress**: Linux core build + `LYNX_TEST=design168` tests (`~/.claude/jobs/f8789b49/tmp/r168q/run/bl.sh`), Windows build to
  /mnt/e/lynx-work/design168q/build-win (`~/.claude/jobs/f8789b49/tmp/r168q/run/win.sh`).
- **Next**: fix build / test failures; full test suite; Windows shots (`LYNX.exe --instrumentshot <dir>
  kepler,keplerexo,transistorbass,sawer,beepmap,fruitygranulizer,wasp lynx|fl`, fresh LYNX_PROFILE_DIR,
  LYNX_TEST_MODE=1) after, and the same from a build of commit 6055913f (before); land only after round 168n has landed
  on main (merge origin/main first).
- **Commands**: syntax check `BUILDSLOT_SHORT=1 coord/buildslot.sh <scratchpad>/q168q/syn.sh <files>`; bank probe
  `<scratchpad>/q168q/probe/build.sh`; the scratchpad is shared with other rounds — use only its q168q folder.
