# REA round 190ej — two overlapping notes of one key on one channel: which voice a note's end ends

Follow-up of round 190eh (issue #158, `REA_rodada190eh` / `/mnt/e/lynx-work/iss158/HANDOFF.md` §7): 7 of its 25 FL renders
differed from LYNX only because LYNX's instruments ended every voice of a key at the first note off of that key. Clean-room:
behaviour from FL's own command-line renders (`FL64.exe /R /Ewav`, headless, no input) and the existing REA notes on each
generator's Voice_Release; no FL code. Work folder: `/mnt/e/lynx-work/voices190ej/` (`py/mkgen.py` projects, `fl/out*`
FL renders, `lynx/before|after` LYNX renders, `py/angen.py` / `py/tl.py` levels).

## 1. Method [EXEC]

One project per generator, made from FL's Empty template; channel 0's plugin, its info and its state spliced from an FL
preset (`Data/Patches/Plugin presets/Generators/<plugin>/…fst`) or, for engines whose presets are not .fst (Toxic
Biohazard, Morphine, GMS) and for sample-based ones, from a channel of that plugin in FL's own demo projects
(`py/grab.py`). 120 BPM (1 bar = 2 s), one pattern, one Playlist clip of 8 bars, key 60 only:

| notes | per voice | one voice per key, the newest holds it | every voice of the key at the first end (LYNX before) |
|---|---|---|---|
| FIFO: A 0–4 s, B 2–6 s | B sounds alone 4–6 s | B sounds alone 4–6 s | silence from 4 s |
| LIFO: C 8–14 s, D 10–12 s | C sounds alone 12–14 s | silence from 12 s | silence from 12 s |

Levels: RMS per 0.5 s (`py/tl.py`), FL's wav against LYNX's `renderwav` of the same .flp (`LYNX_RAW`).

## 2. FL 2026 [EXEC: 32 renders, `fl/out/*`]

| Generator | FIFO (4–6 s) | LIFO (12–14 s) | FL's rule |
|---|---|---|---|
| Sampler (channel sampler, sine sample, volume envelope on) | sounds | sounds | per voice |
| 3x Osc, Sytrus, Harmor, Harmless, Fruity DX10, BeepMap, SimSynth, DirectWave | sounds | sounds | per voice |
| Ogun (Default (tonal)) | sounds (B decays on) | silence: D's note on cut C (Ogun's own same-key cut, REA_rodada37 §1.14), D ends at 12 s | per voice |
| Kepler, Kepler Exo, MiniSynth, PoiZone, Sawer, Toxic Biohazard, Morphine, GMS, BooBass, Sakura | sounds | silence from 12 s | one voice per key: a second note of a held key takes it over (the overlap is no louder than one note), the earlier note's end does nothing, the end of the note that holds the key ends it |
| FL Slayer | sounds | sounds (both decay) | (LYNX already the same) |
| Plucked!, Wave Traveller, Fruity Slicer, Slicex (demo sample) | — | — | the sounds end before the first note off: no evidence from these renders |
| FL Keys, Fruity Granulizer (presets / demo channels) | — | — | FL rendered silence (data not found in a headless render) |

The "per voice" rule matches what FL's SDK hands a generator: every note is a voice (TriggerVoice) and its end is
Voice_Release of that voice — Slicex (REA_rodada190h, VMT slot 10, 0x883970), FPC (REA_rodada190z §1: a note off releases
the layer voices of that host voice), Plucked! (REA_rodada65 §1), Fruit Kick (REA_rodada43), DrumSynth Live (REA_rodada30),
Granulizer (REA_rodada67), Wave Traveller (REA_rodada167f), FL Keys (REA_rodada69). The "one voice per key" engines are
the ones whose cores are keyed by MIDI note (their wrappers turn Voice_Release into a note off of the key only while that
voice still holds the key).

Hosted plugins: Fruity Wrapper sends each voice as its own note on and note off; VST3 note ons carry the voice's id and
the note off the same id (REA_rodada173a). No wrapper option deals with same-key overlaps (FL 2026's wrapper strings:
only "Send release velocity in note off messages"). What the plugin does with a second note of a held key is the
plugin's.

## 3. LYNX

- `NoteEvent::noteId`: `ScheduledNotes` (the ledger every scheduled note passes, round 169d) numbers each note on and
  gives its note off the same number (it already matched offs to notes by their end, so the inner note of a LIFO pair
  gets its own). The echo delay gives each repeat its own id (`noteEchoId`); arpeggio and VFX notes have none.
- The rack sets `Instrument::setNextNoteId` before a note on and `setNextNoteOffId` before a note off, and clears both
  right after. A Mono channel's note offs carry none (its notes share one voice). Live keys carry none.
- Per voice (`Instrument::endsVoice`): Sampler (and Fruity Slicer, FPC's layers, audio clips unchanged), LYNX Synth,
  3x Osc, Sytrus, Harmor, Harmless, DX10, BeepMap, SimSynth (a scheduled note no longer releases the key's previous
  voice at its note on: FL's render plays both), Ogun / Autogun, DirectWave, Slicex, Plucked!, FL Keys,
  Granulizer, Wave Traveller, DrumSynth Live, Fruit Kick.
- One voice per key (`KeyOwners`): Kepler / Kepler Exo, MiniSynth, PoiZone, Sawer, Toxic Biohazard, Morphine, GMS
  (desktop engine), BooBass, Sakura (a scheduled note of a held key also releases the key's voice first: in FL's render
  the overlap is no louder than one note; LYNX played two).
- Unchanged: hosted plugins (two note ons, two note offs, as FL), LYNX Soundfont, Pocket SuperSaw and FL Studio Mobile's
  modules, Drumaxx, Drumpad, BassDrum (notes never shorten), Transistor Bass, FL Slayer, Patcher graphs, MIDI Out.

Results: see `/mnt/e/lynx-work/voices190ej/HANDOFF.md`.
