# Round 190li: vocal tuning, LYNX Newtone measured against Melodyne 5

These are clean-room notes: they record behaviour seen through Melodyne's standalone GUI and its exported audio. No
Celemony code, binary, data or licence file was opened. Melodyne 5 studio 5.4.2 is a trial activated by the owner
(about 2026-12-17) and is PACE-protected, so only the GUI and exported renders were used.

The round's folder is `/mnt/e/lynx-work/vocal190li/` and holds:
- HANDOFF.md;
- STUDY.md (the PitchNet study);
- the Melodyne captures, in `ref/melodyne/` with their session log in `mel/NOTES.md`;
- the LYNX Tune lab, in `tune/HANDOFF.md`;
- the scores, in `out/final*/`.

## 1. The lab (tools/vocallab)

We needed test material whose correct result is known. Recordings don't give that, so the lab makes its own.

- **`vocalsynth.py`** writes sung phrases as a source-filter model rendered additively:
  - each harmonic of the pitch curve gets the level of a fixed spectral envelope (glottal tilt × five vowel formant
    resonances, after Peterson & Barney / Hillenbrand);
  - the phrases have scoops, slow drift, vibrato, jitter, shimmer and aspiration;
  - consonants s, sh, h, t and k are shaped noise.
  
  There are three phrases. `f-phrase` is a female line with 7 notes; one pair is only 40 cents apart. `m-phrase` is a
  male line with 6 notes. `i-legato` is a legato instrument line with 6 notes and glides.

  The **ideal** for every edit is the same phrase rendered from the edited score: what a perfect, transparent editor
  would output. For pitch and formant moves the ideal keeps the source note's loudness. The ground truth (`.json`)
  holds the notes, the pitch curve every 5 ms, the formants, and where the consonants are.
- **`vocallab.py`** scores a render against the ideal:

  | metric | what it measures |
  |---|---|
  | pitch | mean \|cents\| against the ideal's own measured curve, also split into note bodies and joins (±60 ms) |
  | gross | % of frames more than 50 cents off |
  | formant | rms dB between the harmonics' levels and the true envelope at their frequencies, mean removed |
  | flux | excess spectral flux at the joins |
  | hnr | inter-harmonic noise against the ideal, i.e. roughness |
  | consonants | third-octave band levels against the ideal |
  | smear | spread and pre-echo of t / k bursts |
  | level | mean \|dB\| per note |

  Renders are sample-aligned: LYNX by construction, and Melodyne's unedited export has lag 0. `run` drives
  `newtonelab`, the LYNX side, over the matrix.
- **`newtonelab`** (`tools/newtonelab.cpp`, built by `scripts/build-local.sh`) runs Newtone's analysis, an edit and
  the render on a WAV. `bench=N` reports the render's ms and CPU instructions.

The edits are: all notes ±3 and ±7 semitones; one note +3; Correct Pitch at centre 50 / 100 with drift 0, centre 0
with drift 100, and centre 100 with drift 100; modulation (vibrato) 0 % and 150 %; formants +3 semitones; one note
−6 dB; and one note 25 % longer.

## 2. What Melodyne does (Melodic algorithm, the studio trial)

- **Detection.** It found 7/7, 6/6 and 6/6 notes, with no splits and no joins. The 40-cent pair in f-phrase stays as
  two notes. LYNX's FL analysis joins that pair into one note, as FL does. "Separate close notes" (round 190kx)
  separates it.
- **Consonants are never notes of their own.** s, sh and h are drawn striped, attached to the start of the note they
  lead into, or to the end of the previous note after a vowel. t and k appear as a spike in the curve. In the scores
  Melodyne changes consonants by 1.1–2.8 dB in band level on shifts. LYNX reads unvoiced parts 1:1, so it changes them by 0 dB.
- **Note pitch assignment is not always the nearest semitone.** In f-phrase the two notes that start low (E−30 with a
  rise from D, and G−20) were assigned D# and F#. Correct Pitch 100 then pulled them about 70–80 cents down. That is
  why Melodyne scores 35 cents on f-phrase/centre100 against an ideal that removes the detune. Its m-phrase and
  i-legato notes went to the nearest semitone.
- **Pitch moves are relative.** Each note keeps its own deviation (G−23 becomes A#−23).
- **Moving one note** re-joins its pitch curve to both neighbours with smooth S-shaped glides, and the neighbours keep
  their own curves (shot `ref/melodyne/shots/f-phrase-one-up3-zoom.png`).
- **Correct Pitch.** The centre amount pulls each note's centre to its semitone. The drift amount flattens the slow
  wander and keeps the vibrato (drift0: Melodyne 3.9 c on f-phrase).
- **Pitch Modulation 0 %** flattens everything inside the note, vibrato and glides included, so transitions become
  steps. 150 % scales the vibrato depth exactly.
- **The Time tool** lengthens a note and shortens the next one: the next note's start moves later, its end stays, and
  nothing later is pushed. The vibrato is stretched with the note. **Quantize Time** used the detected tempo map.
- **Loudness.** Shifts change the level by 0.3–2.7 dB (down7: 2.6 dB). Formant +3 changes it by 1.1–1.3 dB.

## 3. Newtone engine 1 ("natural", `Track::engine`)

Engine 0 is FL's model as LYNX built it up to round 190kx: TD-PSOLA from peak-picked marks, nearest-mark grains, and
FL's step-and-ramp transitions. Every existing project and every FL import keeps it, bit for bit. Their saved state
stays layout 3 byte for byte unless a layout-4 field is used. New Newtone slots get engine 1. The design is textbook
DSP plus methods from PitchNet (AGPL-3.0, credited in the code):

1. **Marks** are refined by normalised cross-correlation with the mark before, within ±0.15 period of the
   peak-locked mark. Whole samples are kept, so an unshifted part is still the source. Peak picking's
   cycle-to-cycle jitter was the main cause of roughness: hnr fell 5–10 dB.
2. **Voiced runs are extended** back to the voice's real onset and on to its end, by walking periods while the
   waveform repeats (ncc ≥ 0.7). The pitch frames switch on about 20 ms late, which left the onset of a note
   unshifted.
3. **Grains.** Each window half reaches exactly the neighbouring mark, so at no shift the windows sum to one and the
   result is the source. A triangle mixes the grains around the source time; it is wider when shifting down, as an
   anti-alias filter on the sequence of periods. Overlap is normalised where grains stack (up-shifts). The voiced
   output follows the source's 30 ms level.
4. **Unvoiced marks** (consonants, breath, silence) are read 1:1 and never shifted or formant-moved.
5. **Joins** between notes with different shifts are a raised-cosine blend of the two shifts, centred on the
   boundary and lasting the analysed glide (Melodyne's S-curve). FL's step-and-ramp is not used.
6. **Drift and modulation.** The deviation from the note's sung centre (`Note::sungPitch`, from exact periods) is
   split by two passes of a 180 ms moving average into slow drift and fast modulation, which are scaled on their own
   (`Note::pitchDrift`, `Note::modulation`, on top of FL's Variation). The estimator's frame noise (±8 ms) is kept
   as sung, not scaled.
7. **A stretched note holds its vowel.** Its onset glide and unvoiced tail keep their recorded length; this is
   PitchNet's NoteTimeMap method.
8. **The analysis of the last sample rendered is kept**, so later edits re-render without it.

The note analysis (both engines, new analyses only) gained an **octave guard**. Where FL's loose YIN threshold
(0.3) takes a period an octave or more off the strict one (0.15), the strict pitch is used. A low male voice used to
read up to two octaves high: 49.7 % of the frames were more than 50 cents off; now none is. FL's voicing and
segmentation rules are unchanged. `Note::sungPitch` makes engine 1's Center exact: FL's origPitch, from whole-sample
periods, reads up to ~10 cents sharp.

## 4. Scores (out/final/scores.json; e0 → e1 · Melodyne)

The full table is `/mnt/e/lynx-work/vocal190li/metrics-final.md`. Summary over the 42 cases:

- **Pitch accuracy.**
  - Uniform shifts: e1 0.87–1.91 c, e0 1.06–3.52 c, Melodyne 1.04–7.96 c.
  - Correct pitch (m / i phrases): e1 1.22–4.74 c, e0 3.25–15.15 c, Melodyne 1.32–6.13 c.
  - Drift removal: e1 2.59–7.5 c; Melodyne 2.09–3.87 c. Engine 0 has no such tool: FL's Variation also removes the
    vibrato.
- **Formants.** Shifts: e1 1.87–3.77 dB, e0 2.02–4.96 dB, Melodyne 2.37–4.37 dB.
- **Roughness (hnr).** Shifts: e1 −2.78…+5.77 dB, e0 2.21…7.85 dB, Melodyne −1.25…+9.03 dB.
- **Level kept.** Shifts: e1 0.02–0.28 dB, e0 0.55–1.64 dB, Melodyne 0.34–2.65 dB.
- **Consonants.** Both LYNX engines 0 dB; Melodyne 1.09–2.81 dB on shifts, up to 9 dB on its time edit.
- **Engine 1 against engine 0, case by case** (tolerances in `vocallab.TOL`). It is equal or better everywhere except
  four places, all small:
  - formant +0.11 dB on m-phrase/formant+3 and +0.12 dB on i-legato/up3;
  - spectral flux at joins +0.031 on f-phrase/formant+3;
  - inter-harmonic noise +0.6–0.9 dB on mod150. Engine 0 cannot make that edit: its stand-in, Variation 150 %, also
    exaggerates the drift, so its pitch error is 2.6× engine 1's.
  
  The stretch case's reference is a choice: Melodyne shortens the next note instead, so it scores 27–31 c there.
- **CPU** (offline, f-phrase, instructions per second of audio). Engine 0 costs 224–238 M per render. Engine 1 costs
  284–319 M for its first render of a sample and about 100 M for every later edit, because the analysis is kept. The
  ms per second of audio follow the same ratio.

With Separate close notes (`out/final-fine/`), f-phrase changes as follows:
- one-up3 drops from 30 c (FL's joined note) to 0.72 c, against Melodyne's 6.57 c;
- centre100 drops to 1.14 c (e0 15.43 c, Melodyne 35.07 c);
- correct100 drops to 2.6 c;
- gain-6 is exact: in FL's layout the joined note took the next note and the consonant after it down with it.

## 5. PitchNet (SessionLoops, AGPL-3.0): `/mnt/e/lynx-work/vocal190li/STUDY.md`

PitchNet's neural vocoder weights (openvpi PC-NSF-HiFiGAN) and its GAME note segmenter are CC BY-NC-SA 4.0. RMVPE
has no licence. None of them can ship. Its PSOLA engine scored no better than engine 1. From PitchNet, round 190li
reuses three methods, all reimplemented: consonants keep the recording, a stretched note holds its vowel, and the
drift is scaled around the note's pitch. Sibilant Balance is listed for later.
