# REA round 169e — where a new Drumaxx sounds (pad keys, the step sequencer, LYNX's drum banks)

Evidence markers: [DATA] FL's own files read byte by byte (Plugin database, presets, `Data/System`); [FL render] FL
Studio 2026's command-line render (`FL64.exe /R /Ewav`, both FL locks, one launch); [DECOMP] Ghidra on FL's binaries;
[CODE] LYNX source; [TEST] LYNX tests. Clean-room: behaviour and layouts only; no FL preset, sample or state is copied
into LYNX (LYNX's kits keep their own LYNX-designed sounds, only their keys follow FL's layout).

Found by round 169d: without FL Studio a new Drumaxx gets LYNX's kit "Acoustic Mesh" whose pads are on keys 36–43 only,
so the step sequencer (C5 = 60) and key 48 play silence.

## 1. FL's fresh Drumaxx is the Plugin database's "Default Kit" [DATA][FL render]

- `Data/Patches/Plugin presets/Generators/Drumaxx/` has **no `Default.fst`** (only category folders). The plugin's
  entry in FL's Plugin database, `Data/Patches/Plugin database/Generators/Drum/Drumaxx.fst`, and
  `Data/System/DrumaxxTrack.fst` hold the same state: tag `DMS4`, kit name **"Default Kit"**, pads:

  | pad | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11–16 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | name | Bassdrum | Snare L | HH L | HH 2 | Crash | Hi Tom | Snare R | HH R | Clap | Lo Tom | "..." |
  | key (offset 4 of the pad's 44) | 36 | 38 | 42 | 44 | 49 | 45 | 38 | 42 | 37 | 43 | 0 |

  Every pattern's trigger key is 100, no cut groups. So the kit sounds on keys {36, 37, 38, 42, 43, 44, 45, 49} — FL's
  C3…C#4 drum keys (GM-like), never on C5.
- The kit's name and pad names are not in `Drumaxx_x64.dll` / `engine_x64.dll` (strings, ASCII and UTF-16): the DLL
  itself has no "Default Kit". A Drumaxx channel with **no state** (the DLL's own defaults) was **silent on all ten
  test keys** in FL's render (§2, channel 1). The playable fresh Drumaxx a user gets in FL is therefore the Plugin
  database state.
- FLEngine's four `"Default.fst"` references (0x10053dc, 0x1027094, 0x12a3cc4, 0x12e5b48, used by FUN_01004a60,
  FUN_01026ee0, FUN_012a39c0, FUN_012e52e0) are the mixer-track reset ("This will reset the selected mixer track(s) and
  remove all plugins", "reset mixer track(s)") and instrument-track deletion paths [DECOMP]; none loads a generator's
  `Plugin presets\…\Default.fst`. (LYNX keeps preferring a `Default.fst` where FL ships one, as before.)

## 2. What FL plays: steps and keys [FL render]

`C:\Users\kauan\lynx-test\drumaxx169e\fl\dmx169e.flp` (built from FL's `Render tests/Drumaxx.flp`, 120 BPM, PPQ 96,
channel root note C5 = 60): channel 0 = Drumaxx with the Plugin database state, channel 1 = Drumaxx with no state;
ten notes per channel, one every 2 beats, keys 60, 36, 48, 38, 42, 44, 49, 45, 37, 43 (a step of the step sequencer
is a note on the channel's root key, C5 = 60). FL's render `dmx169e.wav` (44.1 kHz, 20 s), peak per note:

| key | 60 (step / C5) | 36 | 48 | 38 | 42 | 44 | 49 | 45 | 37 | 43 |
|---|---|---|---|---|---|---|---|---|---|---|
| Default Kit | **silent** | −16.3 dB | **silent** | −19.1 | −24.5 | −41.0 | −25.7 | −25.7 | −28.9 | −19.9 |
| no state (DLL defaults) | silent | silent | silent | silent | silent | silent | silent | silent | silent | silent |

So in real FL a fresh Drumaxx **does not sound from the step sequencer** (C5) nor on 48; it sounds on its pads' keys
only. ("Every unmuted pad whose Key equals the note plays", REA_rodada67 §8.) LYNX's silence on C5 is FL-exact; what
was not FL-exact is *which* keys LYNX's kits use (36–43 in a row: 39, 40, 41 sounded, 44, 45, 49 did not).

## 3. The other LYNX drum banks of round 168p [DATA][TEST]

- **Fruit Kick, Drumpad, Sakura, Plucked!** are single-voice, pitched: any key plays (the note sets the pitch), the
  step sequencer's C5 included, in FL and in LYNX (test §5). BassDrum and FL Slayer have no LYNX bank (FL's files).
- **Fruity DrumSynth Live**: FL's Plugin database state (`Fruity DrumSynth Live.fst`, version 3, editor key 64) fills
  **all 120 keys**: slot s = key − 48 for keys 48–71, key + 24 below 48, the key itself above 71; slot s holds patch
  s mod 24 of the 24-patch kit ("808 Bongo H" … "909 Snare"), identical except parameter 34 (= the slot's own key: no
  morph). So FL's kit sounds on every key, C5 = "808 Tom H". LYNX's kits had patches on 48–57 only (C5 silent). FL's
  `Plugin presets/.../Default.fst` is a version-1 kit LYNX's engine doesn't read (REA_rodada142). DrumSynth Live is not
  offered as a new instrument in LYNX (it comes from `.flp` files); its LYNX kits are presets for such a channel.

## 4. LYNX before / now

| | before | now |
|---|---|---|
| New Drumaxx, no FL | "Acoustic Mesh", pads on 36–43 | the same kit, pads on FL's Default Kit keys: kick 36, snare 38, closed hat 42, open hat 44, low tom 43, high tom 45, rim 37, wood block 49 (cymbal slot); silent on 60 / 48 like FL |
| The other LYNX Drumaxx kits | 36–43 | Hand Percussion: deep floor 36, wood block 37, tabla 38, closed hat 42, conga 43, cowbell 44, bongo 45, metal clank 49; Industrial: deep floor 36, rim 37, metal clank 38, closed hat 42, lo-fi thud 43, open hat 44, laser tom 45, cowbell 49 |
| A LYNX kit's unused pads (9–16) | volume 0, keys 44–51 | volume 0, key 0 (FL's spare pads are on key 0) |
| New Drumaxx **with** FL | the first preset of the folder, recursively ("Acoustic/70s Beats FG.fst": a pad on 48) | FL's Plugin database "Default Kit" (§1) |
| New Drumpad / BassDrum with FL (no `Default.fst` in FL) | the folder's first preset | its Plugin database `.fst` first (the state FL adds), then the folder's presets. Other generators without a readable `Default.*` (Sakura, GMS, Morphine, Transistor Bass, Ogun) keep the folder order: applying the rule to all of them changed Morphine's fresh sound (the mono/porta test's pitch moved), so they need their own check — left for a later round |
| LYNX's DrumSynth Live kits | patches on 48–57, other keys silent | each one-octave kit repeated every 12 keys over the 120 keys (a patch on its own key wins), like FL's kits that fill every key; C5 plays the kit's C (the kick) |

Code: `core/DrumBanks.cpp` (kit keys), `core/BankStates.cpp` (`drumaxxState` spare pads, `drumSynthParams` repeat),
`core/NativeEngines.cpp` (`presetCandidates`: the Plugin database entry after `Default.*`).

**For the user (decision):** FL's own fresh Drumaxx is silent from the step sequencer; LYNX now does the same. If a
LYNX-own convenience is wanted (e.g. a pad also on C5, or the channel's steps playing the kick), that would be a LYNX
extra, not FL behaviour — not done here.

## 5. Tests [TEST]

`tests/Drumaxx169eTests.inc`:
- `drumaxx169e_fresh_drumaxx_without_fl_sounds_on_fl_keys`: no FL → the new Drumaxx's pads are on {36, 37, 38, 42,
  43, 44, 45, 49}; a step (C5) and Piano roll notes on 60 / 48 are silent, each of the eight keys sounds (no clip); every
  LYNX Drumaxx kit uses exactly those keys, spare pads on key 0 at volume 0.
- `drumaxx169e_lynx_drum_banks_play_from_the_step_sequencer`: Fruit Kick, Drumpad, Sakura, Plucked! sound from a step;
  every LYNX DrumSynth kit repeats every 12 keys over all 120 and sounds on 60, 48, 36, 72; a patch on its own key wins.
- `drumaxx169e_fresh_drumaxx_with_fl_is_fl_default_kit` (FL Data dir): the new Drumaxx is "Default Kit" with FL's keys,
  silent on 60 / 48, sounds on 36 / 38 / 42; the Plugin database comes first for Drumaxx, Drumpad, BassDrum; Fruit Kick keeps its
  Default.fst and has no Plugin database candidate.
- `natives142_engines_from_the_fl_installation` now plays the FL Drumaxx on 36 (FL's kick) instead of 48.

## 6. Windows check [EXEC]

Branch build (`build-win/LYNX.exe`), fresh `LYNX_PROFILE_DIR` + `LYNX_TEST_MODE=1`, CLI modes that exit by themselves
(`C:\Users\kauan\lynx-test\drumaxx169e\run.sh`):
- `--export fl\dmx169e.flp lynx-dmx169e.wav --pattern` — the same project FL rendered (§2), per note:

  | key | 60 (step) | 36 | 48 | 38 | 42 | 44 | 49 | 45 | 37 | 43 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | FL | silent | −16.3 | silent | −19.1 | −24.5 | −41.0 | −25.7 | −25.7 | −28.9 | −19.9 |
  | LYNX | silent | −13.3 | silent | −16.1 | −21.5 | −38.0 | −22.7 | −22.7 | −25.9 | −16.9 |

  Same keys sound and stay silent; every pad is a constant +3.0 dB louder in LYNX's render of this project
  (pre-existing, the same on every pad: a render-level / channel-gain difference, not the pad mapping; left for a later
  round). The stateless channel 1 (DLL defaults, silent in FL) plays a tone in LYNX on every key: LYNX has no model of
  a Drumaxx without a state (FL projects always store one); left as found.
- `--instrumentshot shots drumaxx lynx|fl`: `shots/drumaxx-lynx-100.png` shows "Acoustic Mesh" with pads 1–8 on C3, D3,
  F#3, G#3, G3, A3, C#3, C#4 (FL names: 36 38 42 44 43 45 37 49); `shots/drumaxx-fl-100.png` shows FL's "Default Kit"
  (Bassdrum C3, Snare L D3, HH L F#3, HH 2 G#3, Crash C#4, Hi Tom A3, Snare R D3, HH R F#3).

## Where I stopped

- Done: FL evidence (§1–3), the fix, the tests, REA doc.
- Done also: build-local (1 963 405 checks, 0 failures), Windows build + check (§6).
- Next: tests with the FL Data dir, `coord/train.sh drumaxx169e`.
- Commands: FL render `bash ~/.claude/jobs/f8789b49/tmp/drumaxx169e-flrender.sh` (only if needed again: one FL launch);
  per-note peaks `python3 -I <scratch>/seg.py <wav> 60,36,48,38,42,44,49,45,37,43 1.0`.
