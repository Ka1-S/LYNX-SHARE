# REA round 168l — Channel Misc › Mono / Porta / Slide on every generator (v0.1.22)

Source: the YouTube review of 0.1.21 on macOS (Busy Works Beats), analysed in round 168d
(`docs/analise/REA_rodada168d-video.md`, moment 11, 5:33–5:58, fix round **R-misc**). Evidence frames:
`/mnt/e/lynx-work/video168d/` (f1/s0338, s0350, crops/mono.png).

All FL evidence below is static analysis of FL Studio 2026's own `FLEngine_x64.dll` (Ghidra 12.1.4 headless on the
analysed project `/mnt/e/lynx-work/r167g/flea`, read-only; scripts and raw decompiles in
`~/.claude/jobs/f8789b49/tmp/r168l/d1…d6.txt`). Clean-room: behaviour only, nothing copied. No FL session was needed
(the decompile settles every behaviour LYNX implements; see "Not proven" for the rest).

## The case

| reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| (3x Osc imported from FL → Misc → clicks **Mono** and **Porta** many times) *"this monophonic is not turning on. So I really can't play legato stuff … this is not working"* | Misc › Polyphony (Max, Slide, Porta, Mono) exists on every generator channel (REA_rodada143 B2, [EXEC misc.png]); FL's channel voice code applies Mono / Porta in front of every generator (below) | `channelHasMono()` accepted only the LYNX Synth, `channelHasPortamento()` the Synth and the Sampler: the Misc tab greyed the boxes out for 3x Osc, every FL engine, LYNX's Kick / Hat, hosted plugins (reproduced on Windows main by round 168d: s11-a.png, s9-crop.png, crops/k515.png). The import kept FL's Mono / Porta only in the LYNX Synth's settings, where no FL engine read them | the boxes and the Slide knob work on every channel that makes sound; the rack's channel layer (`core/ChannelPoly.h`) plays FL's Mono and Porta in front of every instrument; the .flp import keeps FL's flags on every channel; old projects get them back |

## FL's channel voice code (FLEngine_x64.dll)

### Voice start `FUN_00d293d0` **[DECOMP]**
- The polyphony limiter (`FUN_00d2bd90`: count the channel's live voices, release the `live − max + 1` oldest) runs
  only when **Mono is off** (`(C+0x1F4 & 1) == 0`), Max ≠ 0 and the channel type is not 2.
- The Gate limit (`C+0x2C4 < 1447`) applies unless Full porta (`C+0x2C2`) and the note's porta bit `0x10`.
- `FUN_00d237e0` fills the new voice; then, if the voice's state is **negative** (Mono's virtual voice, below) the
  generator's trigger (voice vtable slot 0) is **not called**, else it is; in both cases `FUN_00d23db0` sets the
  voice's starting levels.

### Porta: when a voice glides `FUN_00d237e0` **[DECOMP]**
glide = `(channel Porta, C+0x1F4 bit 1) ≠ (note bit 0x10)` **and** the colour group has a last note (slot
`C+0x3A0 + group·0x34` ≠ `0x7FFFFFFF`) **and** porta ticks `C+0x6E4` ≥ 1 **and** the note's arpeggio flag is 0
(`FUN_00d28910`: set when the channel's arpeggiator direction `C+0x2AC` — `0xD7` byte 40 — is on) **and** the channel's
echo feedback `C+0x200` (`0xD1` int 0) is 0. (Same conditions as REA_rodada6 §4.3, re-read on the 2026 binary; the two
"[HYP]" blocks of round 6 are now identified: 0x2AC = arpeggiator, 0x200 = echo delay feedback.)

### How it glides `FUN_00d23db0` / `FUN_00d26460` / `FUN_00d26430` **[DECOMP]**
- With glide, the voice starts with the group's last-note values (5 values: pan, volume, **pitch**, Mod X, Mod Y)
  and each steps by `(own − start) / (ticks + 1)` per tick: **linear, ticks + 1 ticks long**. Without glide the group's
  slot just takes the new note's values. The slot is the last note **triggered** (ended or not).
- `FUN_00d26460` does nothing when the echo feedback `C+0x200` ≥ 1.

### Porta time `FUN_00fea220` → `FUN_0117bce0` = `FUN_0117bbc0` + `FUN_01312200` **[DECOMP]**
`n = round((27^(v/1024) − 1)·48/26)` (v = Slide knob, 0…1660, default 820), `ticks = n · DAT_015d89c4 / 48`
(`DAT_015d89c4` = ticks per step = PPQ/4) → `C+0x6E4`. At FL's 96 PPQ: default 820 → n 24 → 12 ticks → glide 13
ticks = 13/96 beat (68 ms at 120 BPM); maximum 1660 → n 384 → 192 ticks → 193/96 beat.

### Mono `FUN_00d236d0`, `FUN_00feaf70`, `FUN_00d23c00`, `FUN_00d23b80`, `FUN_00d26400` **[DECOMP]**
- A voice is a mono voice when Mono is on, the note is not internal (`0x90000`) and not an arpeggio note
  (`FUN_00d237e0` → `V+0xA4`).
- `FUN_00d236d0`: the voice joins the group's list (`C+0x3AC + group·0x34`). Alone in it → it is the group's sounding
  voice (`C+0x3B4`). Otherwise the new voice gets state **−10** (no generator trigger: a virtual voice) and
  `FUN_00feaf70` makes the **sounding voice glide** (`FUN_00d26400` → `FUN_00d26460`) to the newest voice's 5 values over
  **porta ticks + 1** — the Slide time, **whether the Porta box is on or not**.
- Release (`FUN_00d241e0` → `FUN_00d23c00`): the voice leaves the list (`FUN_00d23b80`); if the list is now empty the
  sounding voice is released; else `FUN_00feaf70` glides the sounding voice **back to the newest voice still in the
  list**. The sounding voice's own release while others are held does not release it (its length becomes
  `0x7FFFFFFD`, "until the list empties").
- Groups: the low 4 bits of the note's flags = the note colour / MIDI channel: 16 independent mono voices.

### What the generator is told `FUN_00f6f240` → `FUN_00fed7c0` **[DECOMP]**
The Mono / Porta boxes' shared OnClick writes the flags (`mono | porta·2`) and calls `FUN_00fed7c0 (channel,
MaxPolySelect)`, which stores Max and calls the generator's `ProcessEvent (1, Max)` (FL SDK's FPE_MaxPoly). **A
generator never learns about Mono**: it only gets the Max polyphony value.

## What LYNX does now

- `core/ChannelPoly.h` (JUCE-free, real-time safe): `channelpoly::Layer`, one per channel in the renderer, does the
  above per colour group (16): Mono's held list, the sounding voice, virtual voices that glide it there and back, the
  release with the last held note; Porta's glide from the group's last note (porta XOR the note's flag, ≥ 1 tick, no
  arpeggiator, no echo feedback); FL's glide time `glideBeats` = (ticks + 1) / 96 beat.
- `RackRenderer` applies it to the song's notes (`renderChannel`) and to live notes, for every instrument except the
  LYNX Synth (its own Mono / Porta) and the Sampler's porta (its own, now with FL's glide time and the ≥ 1 tick rule).
  Max polyphony's limiter does not run on a Mono channel (`voiceLimitFor`, as FL). Stop / jump / panic / cut groups
  clear the held lists; the last-note slot stays (FL resets it only when the channel is set up).
- How an instrument follows it: a Mono note over a held one is `Instrument::slideTo` (every native FL engine, the
  Sampler, SoundFont, Patcher and hosted plugins — a plugin gets a pitch-bend glide, as for slide notes); engines that
  can't move a sounding voice (LYNX's synthesized drums, DrumSynth, Fruit Kick, Plucked!, Wave Traveller) retrigger:
  the old voice is released and the new note starts. Porta: the note starts at the last note's pitch (its cents) and
  its voice glides home through `HostSlide::anchor` (`HostSlide::pendingGlide`, set by the rack around noteOn) — the
  21 native FL engines with per-voice HostSlide say `followsChannelPorta()`; a hosted plugin starts bent and bends back.
  (Toxic Biohazard, PoiZone and MiniSynth only advance a slide while a slide note runs: the glide-in raises that
  flag too; Drumpad's voice now takes the glide-in — the first test run caught both.)
- The 10 FL engines that took LYNX's Synth mono flag as "max polyphony 1" (3x Osc, BeepMap, PoiZone, Granulizer,
  Plucked!, DX10, Sawer, Sytrus, Ogun / Autogun, Fruit Kick) now take the channel's **Max polyphony = 1**, as FL tells
  them (`FUN_00fed7c0`), never the Mono box.
- Model: `ChannelMisc::mono / porta / portaBeats` for every kind (`.lynx` "misc" → "mono", "porta", "portaTime"); the
  Synth's and Sampler's own copies are kept in sync by `setChannelMono` / `setChannelPortamento`. FlpImport sets them
  from 0xDD's flags (a Max of 1 stays Max 1, not Mono). Projects saved before take an engine's flags from its synth
  settings (where the import had put them).
- UI: Misc tab's Max / Slide / Porta / Mono enabled on every sound-making channel (not on controllers, MIDI Out,
  Layer, VFX, Dashboard, Voltage, Fruity Dance, Wasp, video player, audio clips); the Mono tooltip says what it does
  (15 languages).

Tests (`tests/Mono168lTests.inc`): `mono168l_misc_mono_porta_slide_on_every_generator` (the video's case: fails on
main), `mono168l_fl_glide_time`, `mono168l_channel_layer_mono_and_porta_like_fl`,
`mono168l_rack_plays_mono_and_porta_in_front_of_a_plugin`, `mono168l_every_engine_plays_mono_legato_and_porta` (with
FL's data: every engine renders a legato Mono line and a Porta glide; checks every engine that says it follows the
porta takes the glide); updated `rea143b_channel_misc_functions`, `rea139_…` (Mono lifts the limiter). Self-test
`channel_polyphony` (the .lynx round trip and old projects).

## Not proven / not done
- FL glides all 5 per-note values (pan, volume, pitch, Mod X, Mod Y); LYNX glides the pitch only (a legato note keeps
  the first note's velocity / pan).
- FL steps a glide once per tick; LYNX glides per sample (smoother, same length).
- Hosted plugins: FL's wrapper side of a virtual voice is not traced (LYNX: no note on, a pitch-bend glide; the
  plugin's bend range decides how far it can go — LYNX's wrapper option, default ±2 → wider jumps clamp). [INFERENCE]
- Release tails of earlier, already released voices are left ringing on a Mono channel (FL's code does not kill
  them: the limiter is off and the mono list holds only held notes).
- Patcher, Slayer, SoundFont and Transistor Bass follow Mono (their `slideTo`) but not Porta's glide-in (no per-voice
  HostSlide): a Porta note starts at its own pitch there.
- Channel echo repeats (Misc › Echo delay) never join the Mono list: FL's glide is off with echo feedback, and the
  repeats are internal notes.

## Windows check (fresh profile, LYNX_TEST_MODE=1, real mouse)
`C:\Users\kauan\lynx-test\mono168l\`: g4 (3x Osc added from Add › FL engines), g5 (Misc tab: Max / Slide / Porta /
Mono enabled, not greyed as on main), g6 / g7 (Porta and Mono ticked; Mono's hint in the hint bar), g8-after (LYNX
closed by PID, clean desktop).

## Where I stopped
- Done: REA, core layer, renderer (song + live), engines' glide-in, model / import / file / migration, UI tooltip +
  15 languages, tests, Windows build + GUI check, notes-0.1.22.md.
- Full build-local before the last merge: 2105025 checks, 4 failures (BeepMap / Transistor Bass in the engine audit,
  then left out of the timed pitch reads). Merged origin/main aae68291 (166f02e6), i18n regenerated, own
  build-local.sh changes dropped (round 168w). `LYNX_TEST=mono168l` build-local: 127 checks, 0 failures.
- Landing: the coordinator's merge train (no land.sh by this round).
- Rerun: `BUILDSLOT_PRIO=1 ~/.claude/jobs/f8789b49/tmp/coord/buildslot.sh ~/.claude/jobs/f8789b49/tmp/coord/bl-wt.sh mono168l`.
