# REA round 170i3 — Ogun's late release tail, Wave Traveller's older header, Slicex's slice edge

Clean-room: FL's own binaries and FL's own renders only (`Ogun_x64.dll`, `WaveTraveller_x64.dll`, `FL64.exe /R /Ewav`).

## 1. Ogun's voice engine against Ogun_x64.dll [HARNESS]

REA_rodada169l §5 / 169l2 left Ogun's release tail: within ≈ 2 dB of FL for 1.7 s after the note off, then LYNX
falling about twice as fast below ≈ −50 dB (key 72 in `g169l.flp`: −53 / −55 / −60 / −63 / −67 dB in FL vs
−56 / −65 / −68 / −77 / −82 dB in LYNX).

A host harness (`og.exe`, a 64-bit loader of the user's `Ogun_x64.dll`: effSetChunk with the Plugin database state,
note on / note off with FL's note-off velocity, 512-sample blocks at 44.1 kHz) rendered the same notes. Against the DLL,
LYNX's `ogunrender` (the engine alone) and the live rack (key 72, 1 beat, release from the database patch) agree to
0.1 dB in 0.2 s windows down to −110 dB, 4 s after the note off. **The voice engine is not the difference.**

## 2. FL's cut groups never cut its plugin generators [FL render][CODE][TEST]

The faster tail in 169l's LYNX render came from the project, not the engine: every channel of `g169l.flp` is in cut
group 1 cut by 1 (the template's 0x84 = 0x10001), so the "Drumaxx DB" note at 72 s choked Ogun in LYNX (Ogun's choke
is `killAll`, a 10 ms fade), while FL kept the released voice ringing.

Probes (`mkcut2.py`, run time only, nothing shipped): for each of 27 FL generators, its Plugin database state alone on a
channel, a long note then a short one 1/2 beat later, once with 0x84 = 0x10001 ("cut itself") and once with 0. One FL
launch per folder rendered them. Energy of the first note's partials after the second note's start, cut − ref:

| generator | FL (dB) | LYNX before (dB) |
|---|---|---|
| 3x Osc, DX10, FL Keys, GMS, Harmless, MiniSynth, Morphine, PoiZone, Sakura, Sawer, SimSynth, Sytrus | 0.0 (Sytrus −0.5, Sawer −0.4) | −7 … −25 (cut) |
| Autogun, BassDrum, BooBass, Drumaxx, Drumpad, DrumSynth, FPC, Fruit Kick, Ogun, Plucked!, Transistor Bass, Toxic, Kepler | 0.0 (Kepler 0.4) | ≈ 0 (the notes end before the cut) |
| BeepMap / Harmor | −6.9 / +6.3 | −16.7 / −18.8 |

BeepMap's and Harmor's FL differences have both signs and the same partial content in both renders: their random
phases/noise, not a cut (the LYNX numbers are a cut: the partials vanish). REA_rodada165g-flbeta165k §3 had found the same
for Kepler; REA_rodada167h shows FL does cut its Sampler channels with the same 0x84 event (even with a silent cutter).

LYNX now builds its cut targets (`RackState::build`) without FL's plugin-generator kinds (the 26 above plus the
unprobed FL generators Granulizer, DirectWave, Slayer, Wave Traveller, Kepler Exo: same plugin class, inferred). The
Sampler, the audio clip, LYNX's own instruments, VST plugins (not probed) and Slicex / FPC pads (LYNX samplers; FPC's
own pad cut groups) are cut as before. Test `flq170i3_cut_groups_skip_fl_plugin_generators`.

The re-render of `g169l.flp` (key 72's tail, 0.2 s windows from the note off at 71.0 s, FL `g169l.wav` / LYNX):
−37.3 / −37.2, −46.9 / −46.8, −55.4 / −55.5, −63.5 / −63.6, −71.1 / −71.5, −78.8 / −79.7 dB, then FL's 16-bit
render's floor (before: −82 dB at 72.4 s, choked by the Drumaxx DB note at 72 s).

LYNX's `renderwav` still ends the song with `transportStopped` (Ogun: `killAll`). FL's render stops at the song end,
so there is no FL evidence for that tail; left as it is.

## 3. Wave Traveller's older header version [DECOMP][TEST]

FL's own `Plugin presets\Generators\Wave Traveller\Default.fst` holds a state whose tag-1000 block starts with version
1000001 (FL writes 1000005); LYNX refused it. `WaveTraveller_x64.dll`: the state reader `FUN_0074c030` dispatches on
tags 1000 / 2000 / 3000, and the tag-1000 reader `FUN_0074aee0` reads the version word into a local that the next field
read overwrites: the version is never compared. LYNX (`wavetrav::parseState`) now drops it the same way. Test
`flq170i3_wave_traveller_reads_fl_older_header_version` (Default.fst: key 60 selected, no sample, speed 4 beats, a
2-point wave; `go.fst` still reads with its sample).

## 3b. Ogun's delay and reverb switches [CODE][TEST]

From the dead-control audit (round 170i3c): Ogun's delay runs only with the byte after P (`S.b358[1]`, its tempo
sync `b358[0]`) set, and its reverb only with P[53]'s low byte (`designDelay` / `designReverb`). LYNX's Ogun / Autogun
editor had no switch for any of them. FL's Plugin database Ogun has the delay off (tempo sync on) and the reverb on,
so on a new channel the delay knobs P26–P30 changed nothing and the reverb could not be switched off. `ogunParam` / `ogunSetParam` now read / write them (index 53 = reverb on,
64 = delay tempo sync, 65 = delay on: LYNX's indices, not FL host parameters), and the editor shows the three
switches. Test `flq170i3_ogun_delay_and_reverb_switches_change_the_sound`.

## 4. Slicex's slice edge [FL render] — left

FL keeps the previous slice sounding ≈ 450 samples (≈ 10 ms) after the next slice's note starts, the continued audio of
the sample, then a different, quieter signal until ≈ 800 samples. LYNX fades the slice out in 10 ms at its end. The
second part is not explained by the sample: it needs REA of Slicex's voice end. Not quick, listed.

## 5. The FL batch that showed "Problems loading the project"

`flsession3` (FL PID 22908) rendered 48 of `flin3`'s 54 projects (the coordinator's 56 counted two non-project files).
The six missing: `s_sytrus_cut/ref`, `s_tbass_cut/ref`, `s_toxic_cut/ref`. Cause: the probe. `Sytrus.fst` and
`Harmor.fst` in FL's Plugin database are FL 11.1.1 files whose plugin / preset names (0xC9 / 0xCB) are ANSI; the probe
converted names only for FL 8.0 files, so the projects carried ANSI bytes read as UTF-16 ("畓数獲睡"), FL found no such
plugin and opened its dialog at Sytrus (Transistor Bass and Toxic were never reached; Harmor loaded past it as an empty
channel, so its first render was invalid). `mkcut2.py` now converts ANSI names to UTF-16; all 54 names checked
printable; the eight rerun (Sytrus, Harmor, TB, Toxic) rendered 8 of 8 in 17 s with no dialog. The batch script
(`flsession4.sh`) now refuses to start while any FL64.exe runs, and closes only its own FL PID when the window title
shows a dialog (*roblem* / *rror* / *issing*) or no render arrives for 120 s.

## 6. LYNX before / now

| | before | now |
|---|---|---|
| FL generator in a cut group (incl. "cut itself") | choked at the next note | keeps sounding, like FL |
| Ogun's tail in 169l's project | −82 dB at 1.4 s (choked by Drumaxx) | FL's within 1 dB to −80 dB |
| Ogun / Autogun editor: delay on, delay tempo sync, reverb on | missing (delay knobs dead on a new channel) | switches |
| Wave Traveller's FL Default.fst | refused (empty channel) | loads |

## Where I stopped

- Slicex's slice end (§4): REA of Slicex's voice end.
- VST generators in cut groups: not probed (FL's VST wrapper is also a plugin generator; LYNX still cuts them).
- Ogun at the song end (`transportStopped` → `killAll`): no FL evidence either way.
