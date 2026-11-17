# REA round natives190kj (0.1.30) — Harmor: resynthesis

Clean-room, behavioural: what Harmor_x64.dll (FL Studio 2026, SHA-256 23673ad8…5aba, unchanged since round 11) does when
a sample sits in a part's IMG section, in our own words; no Image-Line code, data or samples in the repository. Markers:
**[DECOMP]** Ghidra 12.1.4 on round 75's analysed project, **[ASM]** objdump, **[EXEC]** the installation's DLL run
unmodified in our harness (round 38's `hm.c`), **[DATA]** factory presets. Data and scripts:
`/mnt/e/lynx-work/natives190kj/harmor/` (`bat/` battery, `rea/` decompiles and notes, `tools/`).

## 0. Why the earlier batteries counted resynthesis presets as matching
24 of the 325 installation presets keep a resynthesis image (object mode 1) whose planes are **not stored**: the state
holds the sample's path (`%FLStudioData%\Patches\Plugin presets\Generators\Harmor\Samples\…`, or
`%FLStudioFactoryData%\…` for two X Olbaid presets) and the DLL analyses the file when the state loads. The DLL asks
the host to expand the path (host dispatcher 14, UTF-8 string in, `PAnsiChar` out) [DECOMP 0x86f… load path]; our harness
answered 0, so the DLL found no sample and played the part's timbres — exactly LYNX's fallback, hence "nulls" of −72 to
−85 dB. With the harness resolving the two path variables (`hm2.exe`, `run/hm.c`), FL's output for those presets
differs from the fallback by +2…+7 dB (null): the honest base (`rea/resynth-base-hm2.txt`).

## 1. Loading [DECOMP FUN_008960f0, FUN_00895ea0, FUN_007929c0]
- The string after the image object's 0x2c header: one length byte (0xff → a 32-bit length), the bytes, trailing
  control characters dropped (FUN_0077d050).
- The DLL decodes the file itself (WAV, AIFF, MP3, Ogg Vorbis, FLAC, M4A, REX…; FL's Ogg-in-WAV) to float, mixes it to
  mono, and — when the state was saved in resynthesis mode — analyses it (`FUN_00895ea0`): image settings from the part
  combo block `misc +0x20` (→ part +0x240, FUN_00794260), the soft knee `+0x15c = (misc +0x24 / 250)²`, a stereo
  transform when `misc +0x45` is set and the file is stereo (not modelled; no factory preset uses it).
- The two settings (FUN_00794260):

| misc +0x20 | frames | kept bins | windows (pass 1 / pass 2) | gains |
|---|---|---|---|---|
| 0 | 2048 | every 4th: 255 harmonics of C2 | Kaiser 36 / Kaiser 21.6 (first 63 bins) | 5.44996, ×0.0675629/0.0560012 |
| 1 | 2048 | every 2nd: 511 harmonics of C1 | Kaiser 21.6 / Hamming 0.53836 − 0.46164 cos | 3.47836, ×0.0670461/0.0427859 |

## 2. Analysis [DECOMP FUN_00794510, FUN_00794370, FUN_00794490, FUN_0062c9d0, FUN_0062cb40; EXEC planes]
1. Resampled with FL's resampler at quality index 6 (64-point sinc, `FUN_00620510`) to 2048·C0 = 33488.07 Hz, so bin k
   is k·C0; hop = round(5.805 ms · rate) = 194 samples (5.793 ms per column); columns = ceil((n − 1 + hop)/hop).
2. Per column and window: frame centred on column·hop (zeros outside), window minus its minimum, normalised by its sum ×
   the pass gain; real FFT; plane 0 = |X[k+1]| · gain; plane 2 = ½·frac(arg/2π + ¼) (rounded to nearest), or, where the
   magnitude is 0, twice the previous column's value plus the expected advance `hop·(k+1)/N`.
3. Decimation (every 2^a8-th bin), the expected advance recomputed for the kept bins (`+0x168`).
4. Weight by k+1, maximum M (→ `+0x38`, the pixel law's reference), soft knee `x − (1 − min(x·160/(M·soft),1))²·x`,
   values < 2⁻²⁴ → 0, then pixels `255·log256(1 + 255·x/M)`.
5. FUN_00795720, resynthesis branch, per column: levels (256^(p/255) − 1)/255; **peaks** = every second bin whose level
   is not below its two neighbours (code 524; none → all 524); **regions**: each peak takes the unassigned bins down to
   the previous region and up to the lowest pixel before the next peak (code = peak + 2); **onsets** (3-bin mean level up
   ×2, ×8 below bin 8, and ≥ 0.0025) → 522, column 0 all 522; a bin returning above 0.001 after silence marks its last
   quiet column 523; a broadband onset (≥ half the bins) blends the pixels 0.25/0.75 with the previous column (9-column
   cool-down); plane 1 = frac(2·Δplane2 − advance)·N/hop, normalised by its largest magnitude (`+0x104`, ≥ 1/64).
   [ASM: the tie rules — `<` for the region minimum, `≥` for peaks, cvtps2dq rounding.]
Checked: plane 0 within 0.06 pixel, codes identical wherever the pixel > 1, plane 1 within 2·10⁻⁴ (`tests` and
`rea/planes_*`).

## 3. The voice [DECOMP FUN_0088ab10 0x88d… (part +0x5c = 2), FUN_00873c60, FUN_0088fb80, FUN_00797870, FUN_0088aa00/aa90, FUN_00890150; ASM 0x88aa30]
- Part pitch: harmonic 1 sits at `+0x148` (−3600 or −4800 cents from C5) instead of an octave below the note; at least
  `rows` partials (+4).
- Column time: 5.793 ms per column (instead of 100); the speed and position laws of image synthesis otherwise; the read
  speed `v = Δposition / (columns per block at speed 1)` and a transient offset `+0x170` (±4 columns) that pulls the read
  position to broadband onsets when the image plays at another speed, decaying by clamp(|v|, 10⁻⁴, 4)·0.044 per block.
- Every `+0x164 = max(1, round(176/B))` blocks (a step of `+0x168 = 176` samples at 44.1 kHz) the frequencies are rebuilt
  (bins of the note's harmonic 1, plus plane 1 in the part's frequency mode); in between they follow the pitch from the
  last step's values (saved over f0).
- First block: phases += analysed phase − f·176 (so the partials reach the analysed phase one step later).
- Later steps, when the sharpening (param 86, `(|p|/1000)²`) × min(1.5·|v|, 1) is not 0: predicted phase = phase + f·176;
  negative or zero param 86: only onset bins (522/523) are corrected; positive: region bins are locked to their peak
  (`(pred_peak − pred_i) + φ_i − φ_peak`), onsets corrected, each with an amount min(Hz·k/12000, k), `k = sharpening/176`;
  the correction is `frac(x)·amount` with **round-to-nearest** (cvtpd2dq), added to the frequency.

## 4. Results [EXEC; `rea/resynth-*.txt`, battery `bat/after.tsv`]
- LYNX-made samples (sine, saw, vowel, chord) in FL's "Time stretching and pitch shifting" preset: null −78…−80 dB
  (before: the part played its timbres, +1…+2 dB); with sharpening 750: −84 dB.
- Factory resynthesis presets: see NOTES.md (`harmor/NOTES.md`) for the table. Those whose image plays slowly with weak
  phase locking (Atlantis 0.11×, Soft & warm, Moving choir, Chorus ensemble, Pad Atlantis) keep −15…−26 dB nulls: the
  free-running partials amplify float-level differences of the analysis (IPP's single-precision FFT and phase against
  ours) into phase drift; with the same settings on a LYNX-made WAV they null at −64…−77 dB, so the voice law is FL's.
- Every filter and resonance shape used by the 325 presets is modelled (no "shape … (off)" fallback remains).

## 5. Not modelled
The stereo transform of `misc +0x45` (stereo files only), sample regions/markers and the "Key to time" region map are
taken from the sample as the DLL does only where the file carries none (LYNX's readers keep no cue points), the IPP
single-precision FFT / phase rounding.
