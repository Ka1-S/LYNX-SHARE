# REA round 190pi — slide-note velocity on Sytrus and the shared slide law, Ogun's link velocity, Overcome's intro

FL Studio 2026 (`C:\Program Files\Image-Line\FL Studio 2026`). Clean-room: behaviour only, no code or data copied.
Every fact below is FL's own output: command-line renders (`FL64.exe /R /Ewav`, minimized, no injected input, under the
GUI and FL locks) of minimal projects made from FL's render-test projects (FL 11.1, PPQ 96, 120 BPM: one note record
list replaced, sometimes the generator's state or one channel record) or of cuts of the demo songs. Work folder
`/mnt/e/lynx-work/flparity_190pi/` (`slide/`, `ogun/`, `intro/`, `astes/`: `flp/` projects, `fl/` FL renders, LYNX raws;
`scripts/mkslide.py`, `slidecmp.py`, `track.py`, `tailcmp.py`, `onsets.py`).

Follow-ups of round 190oz (REA_rodada190oz): Overcome's "Flute" silent in LYNX, Ogun's tails in "Astes – Bien Duro",
Overcome's intro 1–3 dB louder than FL.

## 1. Slide notes and the note velocity

| # | Fact | Evidence |
|---|------|----------|
| V1 | Sytrus (default patch and Overcome's Flute state) moves a sounding note's level to the slide note's velocity in FL's n + 1 per-tick steps, **linear in Init.Vol** ((11^(1.28·v) − 1)/10): a note at velocity 0 is silent until its slide note and reaches the slide velocity's level at the slide's end (z1: −36.8 / −23.2 / −17.0 / −14.6 / −11.3 dB at ticks 100 / 120 / 144 / 160 / 191 of a 96-tick slide from 0 to 100 = Init.Vol 0.06 / 0.25 / 0.5 / 0.67 / 1). Successive slides chain from where the previous one left the note (zm: 0 → 71 → 100, a gap, → 47 → 0). Its pan follows the slide note's pan. | [EXEC] `slide/fl/Sytrus__*.wav`, `SytrusFlute__*.wav` |
| V2 | Every held note of the channel takes the slide note's velocity (z2: two notes at velocity 0, one slide note → both at 100; z3: both from 100 to 25). Sampler, 3x Osc, DirectWave, SimSynth, Harmless (VEL → volume off in its test preset: no level move), MiniSynth (reads the velocity at note on only: a velocity-0 note stays silent), FPC (one-shots: only the sounding tail moves) as round 190od found; the cases of V1 (velocity-0 notes, chained slides, two notes) agree with LYNX's shared law within 0.3 dB per tick on all of them. | [EXEC] `slide/fl/<generator>__{s0,s100,z1,zm,z2,z3}.wav`; `logs` of `slidecmp.py` |
| V3 | Ogun (default patch) follows the slide velocity the same way (z1, zm, vo: 0.00 dB per 48 ticks between FL and LYNX after the change of §3) and its pan. | [EXEC] `ogun/fl/def_{z1,zm,vo,vp}.wav` |

**Overcome's Flute.** Its channel arpeggiates (0xD7: direction up, range 1, time 924, gate 48, **Slide on**): FL plays one
voice that glides between the two held keys (the trill) and keeps the velocity it started with (V4). Before this round
LYNX's arpeggiator emitted its glides as ordinary slide notes with velocity 0, so since #236's slide of the note
properties every glide took the arpeggio's voice to velocity 0: the "ramps up and back" round 190oz saw. Every FL song
with a Slide arpeggio was affected, on every generator.

| # | Fact | Evidence |
|---|------|----------|
| V4 | Slide arpeggio, two held notes at 100 / 25: the level stays at the **first held note's** velocity (records 63@100 then 61@25: −11 dB throughout; 63@25 then 61@100: −33 dB; 61@25 at tick 0, 63@100 at tick 24: −33 dB), while the pitch alternates (554 / 622 Hz, ~17 ticks a step). Without Slide each arpeggio note takes its own held note's velocity (−11 / −33 dB alternating). | [EXEC] `slide/fl/SytrusArp__b2…b7.wav` |
| V5 | A song slide note over a Slide arpeggio moves its voice's level like a plain note's (a1, a2, a3, a6 = Overcome's Flute Trill shape: FL and LYNX within 0.03 dB per 4 ticks after the change). Without Slide (b1) the arpeggio's next notes start at the held notes' own velocity again (FL: one blip at the slide's start, then silence). | [EXEC] `SytrusArp__a*.wav`, `b1.wav` |

LYNX (round 190pi): Sytrus takes voice properties (velocity as Init.Vol, pan); the arpeggiator's glides are marked
(`NoteEvent::arpGlide`) and move the pitch only; with FL's slide law a Slide arpeggio's voice has the first held note's
velocity and expression (`Arpeggiator::flSlideLaw`); a slide of the note properties keeps its own tick schedule, so an
arpeggio glide that starts while it runs no longer stops it (`RackRenderer::PropSlide::start / k`).

## 2. A slide note over a chord

| # | Fact | Evidence |
|---|------|----------|
| C1 | A slide note moves **every held note by the same interval: from the highest held key to the slide note's key** — not each note to the slide note's key. Sampler: 60 + 67 held, slide 60 → 53 + 60 (174 / 261 Hz); 60 + 56, slide 60 → unchanged; 67 at tick 0, 60 at tick 24, slide 60 → 53 + 60 (the highest, not the last one); 60 + 56 (either record order), slide 62 → 58 + 62; 64 + 55 + 60, slide 62 → 53 + 58 + 62; a note released before the slide does not count. The same on 3x Osc, SimSynth, DirectWave, Harmless, MiniSynth and Sytrus. | [EXEC] `slide/fl/*__{z2,z3,c1,c4…c9}.wav`, FFT peaks |
| C2 | A slide note moves **only the held notes of its own colour** (FL's note colour = MIDI channel), pitch and velocity: C5 colour 0 + G5 colour 1, slide D5 colour 0 → D5 + G5; C5 + E5 colour 0 + G5 colour 1, slide D5 colour 0 → A#4 + D5 + G5 (the highest of its colour); two notes at velocity 0 in colours 0 / 1, a slide note at 100 in colour 0 → only the colour-0 note sounds; F#5 colour 1 + A#4 colour 0 with slide notes G#5 colour 1 and C5 colour 0 at once → G#5 + C5; a slide note of a colour no held note has moves nothing (C5 colour 2, slide F5 colour 0 → C5). Harmor follows C1 too. | [EXEC] `slide/fl/Sampler__k1…k6.wav`, `Harmor__{z3,c1,c4}.wav` |

LYNX slid each held voice to the slide note's key (the two voices then sounded in unison, with phase-dependent
cancellation: FL 207 + 261 Hz, LYNX 261 Hz only), whatever its colour (Lightning's resynthesized vocals pair each slide
note with the held note of its colour: colours 0 and 1 slide at once). Now, with FL's slide law (Song::slideNoteProps:
.flp imports and new projects), the rack works out the held keys of the slide note's colour (their colours are kept from
their note-ons) and passes the interval and those keys to the engines' HostSlide (`HostSlide::pendingShift`; a voice's
key is read back from the engine's own target) and to the Sampler; the slide of the note properties moves the same
keys; a slide note of a colour no held note has moves nothing. One held key of the slide note's colour and nothing else
slides as before (bit for bit). Not covered: hosted plugins (one pitch
bend per channel), Patcher and LYNX's mono engines (Slayer, Transistor Bass, …) keep their own slides.

## 3. Ogun: link velocity

| # | Fact | Evidence |
|---|------|----------|
| O1 | Ogun's default patch and Astes' "3.3.3 Chase" scale with the note velocity exactly as FL's Init.Vol: velocity 25 / 50 / 128 against 100 = −21.7 / −12.7 / +6.3 dB (−21.71, −12.70 measured). Astes' "3.3.2 Tibetan" does not (its patch maps the velocity itself; LYNX matched it already). | [EXEC] `ogun/fl/{def,t264,t262}_v*.wav` |
| O2 | REA_rodada37 §1.4: Ogun asks the host's voice event 6 at TriggerVoice when byte `+0x20a` is 0; REA_rodada28 §4.1: event 6 with 0 = "link velocity off" (Final.Vol = channel volume). So with `+0x20a` set the host keeps link velocity on: Final.Vol = Init.Vol · channel volume (O1: the patches that scale have it set). | [DOC] + [EXEC] O1 |
| O3 | Autogun's default patch scales the same way (velocity 50 / 25: −12.6 / −21.5 dB in FL; LYNX ignored the velocity). | [EXEC] `ogun/fl/ag_v*.wav` |
| O4 | Held step notes, a step note's end at the channel's next note, same-key steps and a chord of steps on Ogun (Tibetan and Chase states, 7 cases each) already matched FL (within 0.5 dB per 0.5 s, the Chase patch's random spectrum aside). | [EXEC] `ogun/fl/t26{2,4}_o*.wav` |

**Astes' tails.** The "8–10 dB above FL below −55 dBFS" of round 190oz came from Chase: its patterns "3.3 Metal #2" ff.
play velocity 65–85 step notes, which LYNX played at full level (+4…+7 dB at every level, so most visible in the
tails; at velocity ~100 the level matched). Ogun's voice release and end were not the cause (O4).

## 4. Overcome's intro: FL 11's plugin-parameter numbers and Sytrus' main page under automation

| # | Fact | Evidence |
|---|------|----------|
| I1 | Bars 1–16, each group of the intro's channels solo (its automation clip channels on, the others off): "DX UniLead" (27) +2.5 dB and "Saw Pad Deep" (38) +3.6 dB in LYNX; the other channels within 0.4 dB (Dyno EP 61–64 −0.1, Flying Waltz / Shadow Whisper 42–43 +0.4, Vinyl 82–85 −0.8). | [EXEC] `intro/fl/i*.wav` |
| I2 | Without their automation clips both match FL (j38 +0.03 dB, j27 −0.08 dB); with "Saw Pad Deep Cut" / "Res" / "Vol" or "DX UniLead Cut" / "Res" alone FL changes and LYNX did not change at all. | [EXEC] `intro/fl/j*.wav` |
| I3 | The clips' links (0xE3) target **0x2012 / 0x2013 / 0x2000** of their Sytrus channels: FL 11 numbered a channel's plugin parameters from 0x2000 (FL 12 on: 0x8000; across FL's 165 files only Overcome 11.1.0 and Housework 11.0.3 use 0x2000…0x7FFF, every FL 12+ file 0x8000). LYNX found no target for them. | [DATA] scan of every demo's 0xE3 records |
| I4 | Sytrus' parameter 0 / 1 / 2 = the main page's master level / LFO / pitch (main ints 0 / 1 / 2), **18 / 19 = Mod X / Mod Y** (main ints 190 / 191): the DLL's own ProcessParam(get) returns those ints for Overcome's states (128 / 128 / 0, Mod X 512, Mod Y 0 / 256). | [EXEC] `run/syt.exe … get=0…59` (round 31's harness, rebuilt by round 190oz) |
| I5 | A clip value v reaches the parameter as low + round(v·(high − low)) over the control's range (master 0…160, LFO −128…128, pitch −24…24, Mod X / Y 0…512), and the **sounding voices follow** the master level and the Mod X / Y map positions: with both LYNX renders within 0.2 dB of FL per 2 s (j38c, j38r, j38v, j27c); taking Mod X / Y at note on only left j38c 2.1 dB off. | [EXEC] `intro/fl/j*.wav` vs `intro/dev7x` |

LYNX: `flp::parseProject` moves FL 11's 0x2000-based channel parameters to 0x8000 (links, control values, init and
pattern control events); Sytrus takes automation on parameters 0, 1, 2, 18, 19 (`sytrus::automatable`,
`Engine::setMainValue`). Other Sytrus parameters (operators, filters, the matrix) are still not automatable in LYNX.

## 5. Numbers (LYNX before → after this round, against FL 2026's renders)

- Overcome's Flute solo (172–184 s): peak −49 dBFS (before #236's law: silent) → −16.8 dBFS (FL −16.9); RMS +0.9 dB over
  FL (the patch's random unison: ±1–2 dB in the minimal renders too).
- Overcome's intro, bars 1–16: +1.46 → +0.03 dB (DX UniLead +2.55 → −0.00, Saw Pad Deep +3.60 → −0.03).
- Overcome, whole song: RMS +0.18 → −0.08 dB, 50 ms level error median 0.44 → 0.34 dB, within 3 dB 90.5 → 96.4 %.
- Astes' Chase solo (automation on): RMS +3.93 → +0.12 dB, every 20 s within ±0.4 dB.
- Sytrus Essentials, first 110 s: −0.51 → +0.00 dB (0–30 s −2.9 / −3.9 → 0.0).
- Lightning's resynthesized vocals (Harmor, channels 55 + 58 solo): RMS +1.74 → +0.05 dB (their 50 ms error is Harmor's
  own, 4.6 → 4.4 dB); the whole song's RMS moves −0.72 → −0.90 dB because the other channels stay ~1 dB quiet.

Gold renders, tests and the Sytrus gate: `HANDOFF.md` in the work folder.

## Open

- The pitch of a song slide note while a Slide arpeggio glides: FL drifts the arpeggio's pitch only during the slide
  (a5 / b8: ~+25 cents during the slide, back after it); LYNX's glides override the slide's pitch from the next step.
  Level and velocity match (V5); pitch detail not modelled.
- Which Ogun / Autogun states have `+0x20a` set was read from FL's behaviour of three states (O1, O3) and the decompiled
  event-6 condition (O2), not from a sweep of all 20 demo channels in FL.
