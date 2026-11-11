# REA round 190l: Slicex as a channel, and FL's renders of edited states

Continues `REA_rodada190h.md` (state, regions, key map, articulators, voice render). Clean-room: FL Studio 2026 was
only run and its output measured; no Image-Line code or data.

## 1. Wiring (slice 1)

`InstrumentKind::slicex` (`core/SlicexInstrument.{h,cpp}`): FL's state in `ChannelDef::pluginState`, deck A's sample
in `samplePath`, deck B's in the channel's one zone (`zones[0].path`). Chosen because both fields already load, save
(.lynx), relink (missing files) and preload with the project; nothing new to serialise. The state written to an .flp
is FL's own, which still names FL's files or holds their WAVs, so the round trip through .flp does not depend on the
two paths. Regions: a deck whose WAV is in the state gets FL's regions (lengths, names, keys); a deck that names a
file gets the cue points the sample loader read (markers only: region lengths and keys of a referenced file are not
read yet).

What the host hands the plugin (as Wave Traveller): `Init.Vol = (11^(1.28 v) − 1)/10` times the channel volume, pan,
`100·(key − 60)` + channel pitch + note cents, the note colour (deck, reverse) and Mod X / Y as −1…1.
**"Link velocity to volume" off = every note at 100 %** [EXEC: FL render `vel_off`, residual −80 dB].

The FLP import takes the new kind when the state parses and a deck's audio is on the machine; the sampler on the
slices stays as the fallback. Projects saved as .lynx before this keep their sampler channel and sound the same.

## 2. FL renders of edited states (slice 2)

Method: `Slicex_vel64.flp` (round 170i2: 10 notes of velocity 64 on "DL Breaker", 22 regions) with its 0xD5 event
replaced by `slicex::write()` of an edited state (`p/sxmake` in /mnt/e/lynx-work/slicex190l), rendered by FL (`/R`,
16-bit), compared with the engine alone (`p/flcmp`: least-squares gain, residual relative to FL). FL's render of the
unedited copy is byte-identical to 170i2's. A residual around −70 dB with a worst sample ≤ 1 LSB (3·10⁻⁵) is the
16-bit file's quantisation; quiet renders (band-pass, low master volume) show the same error against less signal.

| edit | before | after | what it settles |
|---|---|---|---|
| low-pass order 1, 2; band-pass 1, 2; notch 3; high-pass 1, 3; group CUT / RES offsets | −50…−77 dB, worst ≤ 1 LSB | same | the filter laws, stage counts and Q / bandwidth scaling |
| cutoff envelope (ENV wheel 200), with and without the sustain point | −72 | same | envelope depth on the cutoff |
| volume envelope: FL's default, no sustain | −70, −69 | same | |
| volume envelope ADSR points (decay 1, sustain 2) | −59 | −68 | see §2.1 |
| the same with ATT / DEC / SUS / REL = 220 / 60 / 90 / 40 | −70 | −70 | the knob factors and the sustain shift |
| the same, tempo-based | −45 | −67 | see §2.1 |
| LFO on volume, local phase; on pan, free time, with tension / skew / pulse width; on cutoff with master LFO 100; on speed | −71…−73 | same | LFO speed law, shapes, depths |
| LFO on volume, FL's default flags (tempo + **global phase**) | −14 | −75 | see §2.3 |
| region pan, volume, delay, start | −71…−77 | same | the pan matrix, region volume law (ln 3), delay, start |
| region pitch (±, up to an octave) | −40 | −72 | see §2.1 |
| "Sample start covers whole length" | −36 | −69 | see §2.1 |
| "Loop half of regions", "HQ envelopes" (4-frame sub-block), master volume and pitch, velocity → volume map | −66…−72 | same | |
| cut group on every region + volume envelope | −30 | −70 | see §2.2 |

### 2.1 A region that ends before its note

None of 190h's material had one (every slice was cut by its note off). FL does not go silent on the region's last
frame: the read goes on with the position past the end and the taps clamped to the last frame (the interpolation rings
out on that held level for a kernel's half width), the voice goes to state 9, and the 20 ms closing declick fades the
held level. The engine wrote zeros past the end. This is what the ADSR, pitch and start-offset rows were missing.
Taps before the region's start still repeat its first frame (consistent with the pitch and start renders).

### 2.2 Cut groups

REA_rodada190h §8 holds: the cut voice goes to state 9 and its declick starts at once. The error was in the envelope:
"all its articulator states 9" stops each envelope **where it is**; the engine's `finish()` left the segment's start
level in place, so a cut voice jumped back up (a 1 → 0 volume envelope cut half way played at full level into the
declick).

### 2.3 Global LFO phase

Confirmed by the render: with flag bit 1 the LFO's phase at the note is `startPhase + phaseOffset + round(u · speed)`
(32-bit wrap), `u` = the song position in the LFO's time unit (beats when tempo-based, half seconds otherwise) and
`speed` the per-unit increment of §4.1 of 190h, i.e. the LFO runs as if it had started with the song. The engine takes
the position through `Engine::setSongPosition`; the channel instrument derives it from the rack's block position and
the frames rendered since.

### 2.4 Layering: not render-checked yet

A second deck does not layer by itself: with deck B a copy of deck A and "Layer regions of same deck only" off, FL
plays exactly what it plays without deck B (and so does the engine's key map): only regions that carry their own key
share a note. To put two regions on one key the test projects got a trigger list (`LIST/tlst`, the layout of
REA_rodada190h §3.1) added to the deck's WAV by hand. **FL ignored it**: its 13 renders are sample-identical to the
unedited project's, while the engine (whose reader is assumed symmetric to FL's writer) layered. So either the
hand-made chunk is not what FL's writer produces (placement, cue id base, a field) or the reader is not symmetric; no
FL-written file with region keys exists to tell. Next step: set a region's key in FL's own Slicex (region settings),
save, and read what FL wrote; then re-run the `key_*` projects of `p/sxmake`.

Until then the layering modes stand on the REA reading alone. "Song time" (mode 7) is now implemented from it
(REA_rodada190h §3.1: the deck sample looped over the song at its own tempo picks the layer whose region the song is
in; crossfade 0 rounds) instead of playing every layer; it only acts on keys with two or more regions.

## 3. Still open

- The trigger list as FL writes it, and with it every layering mode against a render (§2.4).
- Region lengths and keys of a deck that names a file (needs the file's bytes at rack build; the loader gives cue
  starts only).
- Reverse (note colours 14 / 15) past the region's start, the output track offset, a deck's beat grid for a newly
  loaded sample: no render yet.
- The LFO wave at pulse width == 1 (190h art-notes §4.1.6).
