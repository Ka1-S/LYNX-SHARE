# Round 190ab: FL Studio Mobile's GMSynth as its own engine ("GMSynth (FLM)")

Clean-room notes. Source of the laws: FL Studio Mobile_x64.dll as installed with FL Studio (module type 2, 75
parameters), read as behaviour and restated here as formulas and constants. No code of the plugin is reproduced.
Implementation: `src/core/GmsMobile.{h,cpp}`, played by the GMS instrument when `ChannelDef::modelParams = { 1 }`
(`gmsIsFlm`), written by `FlmImport.cpp` (`gmSynthState`). Tests: `tests/FlmGmsynth190abTests.inc`.

Marks: **PROVEN** = read in the code path that renders; **MEASURED** = also checked against a render of FLM inside FL
(section 8); **INFERRED** = read from a decompiled listing only, or one assumption left.

## 1. What the module is

Not the desktop GMS: the same family (wave spectra, 512 band-limited tables, dithered phase read, envelope follower,
voice-steal rule) with another voice. Two oscillators (A, B) + noise, four modulation kinds, a linear amp ADSR, two AD
envelopes (osc mod, filter), one ADSR "EG", one tempo-synced LFO, a 4-voice choir, 4 voices, an 8-pole filter whose
coefficients come from a data file, three effect slots of ten kinds, master level and pan. Control runs in chunks of 32
samples; envelopes, smoothing, oscillators and the filter run per sample.

Data files (the user's installation, read at run time, never bundled):
- `GMSynthWaveshapes/<name>.spectra`: 512 magnitudes then 512 phases (float32) of harmonics 1…512; 29 built-in names,
  index 2 is the file "Devils Horn".
- `FilterTable_Ctf_Res.data`: pairs (g, k) of float32.

Module chunk `CSTM` (20 bytes): i32 wave A, i32 wave B, f32 bend range (round(3v) → 1, 2, 7, 12 semitones), f32
mod-wheel amount, f32 mod-wheel destination. Without the chunk both waves are Sine (18).

## 2. Tables and oscillators

- Table h (h = 1…512 harmonics) = Σ A_k·cos(2π·k·i/1024 + φ_k), k ≤ h. **PROVEN**: the table builder's vector
  routine is a cosine (the SSE path folds |x| and π/2 − |x| without restoring a sign). This settles the open point
  "sine or cosine" of the round-190u spec. MEASURED on the default patch (harmonic levels, section 8).
- Table choice for frequency f: ⌊fs/(2f)⌋ + 1 harmonics, all 512 below fs/1024; none above fs/2. Oscillator A picks
  its table from the unscaled frequency (INFERRED for SNC, where A's frequency is scaled afterwards), B from f·ratioB.
- Phase: 32 bits, read with a 22-bit random dither; generator s ← s·0x0bb38435 + 0x3619636b (shared with the desktop
  GMS). A fresh voice takes its start phases from a second generator, s ← s·0x53c29b01 + 0xa7deab40, two steps per
  choir voice. FLM has one process-wide generator; LYNX gives each instance its own, started at 0, so the dither, the
  noise and the start phases differ from a given FLM render by design.
- Frequency: 220·2^((P − 57)/12), P = key + transpose + bend + glide + pitch modulation. Osc B: × 2^((⌊48·v11 − 24⌋ +
  (v12 − 0.5))/12). Choir voice u of n: × 2^((u·v44/(n − 1) − 0.5·v44)/12); odd voices × 1.5 (QNTA) or × 2 (OCT).
- Mix: m = 2·v13 − 1; A × (1 − |m|)·(1 − n), B × m·(1 − n), noise uniform ± n, n = v20².
- Osc B Phase: at a voice start phase B = frac(phase A·fB/fA + v14).

Modulation amount per sample M = clamp(base + env·(2·v19 − 1) + EG and LFO parts, 0, 1), smoothed 0.01 per sample:
- AM: A·(1 + 3·M·B); FM: phase A advanced by M·B; SNC: frequency of A × 2^(amount), hard-synced to B; FLT: cutoff
  + 0.5·M·B.
- **Quirk (INFERRED from the listing, MEASURED):** the smoother's input is base + 0.01·(velocity gain − base), so
  with Osc Mod at 0 the amount rests at 0.01·w and the default patch carries a trace of AM: a second harmonic
  46.9 dB (key 60, velocity 80) and 43.2 dB (key 48, velocity 120) below the fundamental, the same in FLM's render
  and in LYNX's (±0.3 dB).

## 3. Envelopes, glide, voices

- Follower x += (target − x)·c. Attack c = 1 for v = 0, else 8.82/(100·v⁴·fs), toward 5; at x > 1: x = 1 and the
  decay starts. Decay c = 8.82/((200·v⁷ + 0.001)·fs). Amp release c = 8.82/((v + 0.001)·fs); EG release
  c = 8.82/((200·v⁴ + 0.001)·fs). Sustain levels v².
- The amp envelope is used linearly (not squared as on the desktop) and keeps its value at a retrigger; the two AD
  envelopes and the EG restart from 0. A voice ends when the amp is below 1e-4 with target 0.
- Velocity: the note's gain w is used as it is (linear), smoothed 0.01 per sample (a fresh voice jumps).
- Glide: per chunk of n samples g += (t − g)·n/(0.1·v29·fs + 1), snapping when it would overshoot. **PROVEN** that
  the unknown factor of the 190u spec is the sample rate (the caller converts the module's rate to float for that
  argument). Auto Portamento on: only when another key is held.
- Four voices. A new key takes the free voice with the highest index, else the voice with the smallest start counter.
  Single Voice: voice 0, legato; a note-off returns to the highest held key below, else the lowest above.

## 4. Filter

- Cutoff u = clamp(v21 + key tracking, 0, 1) smoothed 0.001 per sample, plus per sample: filter envelope·c³, EG·e³,
  LFO·a³, FLT modulation. Index ⌊clamp(u)·65534.99⌋ into a table of 65536 (g, k) pairs.
- The run-time table's entry i is read from the file's entry ⌊(2^(i·1.4145113e-4 + 5) − 11)·3.336982534752279⌋
  (32 Hz at i = 0, about 0.3 Hz per file entry), clamped to the file's last pair. **PROVEN**, MEASURED through the
  LP render (24 harmonics within 0.13 dB).
- Eight one-pole stages y ← y + g·(x − y) in series, input x = s − r·k·y8 (r = clamp(v22)). LP = stage 4, "BP" =
  stage 8, HP = s − stage 1 (kind ⌊2·v23⌋). Key tracking: + (P − 64)·0.0089963655 when on.

## 5. LFO, master

- LFO: period {0.25, 0.375, 0.5, 0.75, 1, 2, 4, 8, 16, 32} beats (⌊9v⌋), sine / rising saw / rect, phase re-locked
  to the song position each chunk while the host plays. Destinations ⌊4v⌋: cutoff (a³), osc mod (|a|a), pitch
  (12·|a|a semitones), level (12·a dB), pan (|a|a). EG destinations ⌊2v⌋: cutoff, osc mod, pitch.
- Choir: n = 1 + ⌊3·v42⌋ voices; stereo s_u = 2u/(n − 1) − 1, L = clamp(1 − s_u·v45), R = clamp(1 + s_u·v45);
  normaliser 0.5/((n − 1)·0.15 + 1). The default patch is therefore 0.25·w·wave per side before the filter. MEASURED:
  −0.02 dB against FLM.
- Master: dB = 66·v1 − 48 (+ 12·LFO·a), below −60 → 0, gain 10^(dB/20); pan q: right × min(2q, 1)², left ×
  min(2 − 2q, 1)². Both gains smoothed 0.01 per sample, jumping when the module was silent.

## 6. Effect slots (three, parameters 46 + 7·slot …)

Kind ⌊9v⌋ computed in float (so the default 0.11111111 selects kind 1). X, Y clamped 0…1. Effect LFO: sine, 1 − 2φ,
rect; amount ± (2v − 1)²; period as the main LFO's table, stored doubled for DLY. A slot fades in and out at 0.005 per
sample. FLM keeps one object per kind and slot and cross-fades when the kind changes; LYNX keeps the selected kind
only (a .flm's kinds are static) and starts an enabled slot faded in.

| Kind | Law | Status in LYNX |
|---|---|---|
| DIST | sample-and-hold of clamp(Y⁴)·256 + 1 samples; held value x/(|x|·k + 1)·(0.1 − 0.09·Y)·(k + 1), k = 10^(4·(X + lfo) + 1); no fade | implemented from the law (PROVEN, MEASURED) |
| PHAS | six one-pole all-pass stages, a = (1 − w)/(1 + w), w = (100 + X²·(1 + lfo)·39 900)·0.5/fs clamped 0.001…0.499; feedback 0.98·(0.6·Y)^(1/3); wet added to dry; tail 0.25 s | implemented (MEASURED: the phaser moves the harmonics by −7.7…+11.6 dB, the same in LYNX to 0.1 dB) |
| F LP | four biquads with one sample between stages; f = 2^(8.28·clamp(X + lfo) + 6) Hz, q = 2·Y² + 1; cross-faded with dry | implemented (MEASURED) |
| DLY | two lines of 1.5 s; delay = slot period in seconds, halved while above 1.5 s; write = fade·input + wet; wet = line·X² through a one-pole low-pass (coefficient 0.01 + 0.99·min(2Y, 1)), one sample later a one-pole high-pass (0.45·max(2Y − 1, 0)); output += wet·(1 ± lfo) per side, LFO per sample; a new length clears the lines; runs on until 1.5 s of silence | implemented (MEASURED: echoes −12 dB apiece down to −88 dB within 0.5 dB) |
| RVRB | FLM's plate engine (the one behind its Reverb modules, rounds 182c / 188h): decay ⌊1 + 199·0.75·X³ + ½⌋ tenths of a second, damping ⌊5 + 216·Y² + ½⌋ ×100 Hz, low cut 317 Hz, no high cut, room 100, diffusion 100, no predelay, dry off, early reflections muted, wet 0.3·10^(t/200) with t = ⌊−361 + 421·(0.5 + 0.15·X³) + ½⌋; input × fade, output added; stops after 0.25 s of silence. **PROVEN** (this corrects the 190u spec, which had Y² as the decay) | implemented on LYNX's Reeverb 2 plate with the wet gain applied exactly. Not reproduced: FLM's second input-diffuser coefficient (round 188h). MEASURED: the same decay rate (−21.9 dB/s on both), the tail 0.6…1.1 dB louder in LYNX, ±2 dB per third-octave band |
| PAN | two lines of ⌊0.002·fs⌋ samples; position X + lfo and depth √Y smoothed 0.005; the far side delayed and lowered | implemented (INFERRED; measured only at the centre position, where it is a pass-through) |
| FLNG, F HP, RING, VOX | not read in detail | pass through; the import report lists them when switched on. None is switched on in FLM's shipped content |

## 7. Import (`gmSynthState`)

- Module type 2 with at least 75 parameters, the resource folder known and the three data files present (the filter
  table, both waves' spectra) → instrument GMS, `modelParams = { 1 }`, state "FLMG" + u32 1 + 75 floats + the 20 CSTM
  bytes, `samplePath` = FLM's resource folder. Report: converted "FL Studio Mobile: GMSynth".
- Otherwise today's behaviour: the built-in synth and "FL Studio Mobile: GMSynth" under not converted.
- Listed as partial: each enabled slot whose kind is not rendered ("FL Studio Mobile: GMSynth effect <KIND> (not
  rendered)").
- Not converted, not listed: mod wheel and pitch bend (a .flm lane never carries them for this module; the engine
  plays both at rest), parameters 7…10 and 67…73 (not read by FLM's own render path either).
- Host side: note velocity = the voice's gain, note pan = a linear balance, channel volume and pan on the module's
  output, channel pitch added to every voice.

## 8. Measurements (FLM inside FL through "FL Studio Mobile Rack", against LYNX's import of the same module)

Ten renders, 2026-11-10; method of round 190u (energy ratio, level per 125 ms, third-octave bands, best-lag error per
note) plus the levels of the harmonics of the first two notes. Sets: the default patch (Sine), Saw with LP / "BP" /
HP at cutoff 0.5, Saw with one effect each at X = Y = 0.5, Saw with Slide Time 0.5.

| Set | Energy FLM − LYNX | Level mean / max | Harmonics, max difference | Note |
|---|---|---|---|---|
| default | −0.02 dB | 0.05 / 0.36 dB | 0.27 dB (4) | waveform error −36.8 dB; the AM trace matches |
| LP, resonance 0.8 | −0.02 | 0.14 / 1.39 | 0.04 dB (16), 0.22 dB (32) | |
| "BP", resonance 0 | −0.02 | 0.10 / 1.49 | 0.03 dB (9), 0.21 dB (18) | |
| HP, resonance 0.8 | −0.02 | 0.14 / 0.95 | 0.05 dB (40), 0.30 dB (40) | |
| RVRB | +0.01 | 0.25 / 1.30 | 1.0…2.6 dB | tail 0.6…1.1 dB louder in LYNX, same decay rate |
| PHAS | −0.02 | 0.10 / 1.42 | 0.06 dB, 0.30 dB | |
| F LP | −0.02 | 0.11 / 1.55 | 0.03 dB (7), 0.13 dB (14) | |
| DIST | −0.01 | 0.03 / 0.30 | 1.2 dB | the hold counter runs free: its phase against the note differs |
| DLY | −0.02 | 0.13 / 0.81 | 0.08 dB, 0.39 dB | echoes within 0.5 dB down to −88 dB |
| Slide Time 0.5 (+ PAN at centre) | −0.70 | 0.36 / 3.04 | — | pitch track equal window by window; 3 dB apart only while keys 36 and 48 sound together (an octave pair: its sum depends on the random start phases) |

The level maxima are the windows at note boundaries (FLM inside FL starts a note up to one block late). The waveform
error is bounded by the random start phases and the dither (section 2), so level and spectrum are the criteria. The
second note's −0.1 dB is the test renderer's 8-bit velocity, not the engine.

## 9. Desktop GMS

Untouched: the instrument only branches to the new player for a channel with `modelParams[0] ≥ 0.5`. Gate: 34 of
FL's own .gmsynth presets through the instrument and the reference renderer, previous object against this round's:
34 of 34 byte-identical, CPU 4549 ms before / 4545 ms after (instrument part, -O1 objects).
