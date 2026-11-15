# Round 190gx — Live 190gu facts applied (LYNX SHARE 0.1.28)

Evidence: the 190gu live screen session (`/mnt/e/lynx-work/live190gu/FACTS.md` §A, shots and renders there). Black-box
renders and screenshots of Live 12.4 only; nothing of Live's is in the repo.

## 1. Clip envelopes (corrects round 190gi)

| Fact (190gu) | LYNX before | LYNX now | Where |
|---|---|---|---|
| Live plays clip envelopes only on warped clips; an unwarped clip's Clip View has the Sample tab alone (renders g48 e1–e3: 0.00 dB) | imported and played them | .als import leaves an unwarped clip's envelopes out (listed in the report); the export writes every audio clip warped; the Clip View shows the Envelopes tab only while the clip is warped | AlsImport.cpp addAudioClip, ClipAudioPanel.cpp applyTab |
| Clip Gain law (renders g2 e1/e2/e4, 1 kHz sine): dB = 20·(L − 1) for L ≥ −0.2, steeper below (−34.8 dB at −0.5, −63.2 at −0.9), −1 silent, never above 0 dB | value 0…1 ↔ −1…1 linear | `clipenv::liveGainDb`: 20·(L − 1) down to −0.2; below, max (−50·L² + L − 21.8, −69.4 + 51.35·(L + 0.98)); every one of the 28 measured values within 0.01 dB. Lane values convert through the gain both ways; volume ramps are cut into pieces until the gain agrees within 0.05 dB (Live's ramps are straight in its value, LYNX's in the lane's) | ClipEnvelope.h, AlsImport.cpp, AlsExport.cpp |
| Track Panning (g2 e3): constant power, +3 dB at the sides | LYNX's voice pan is that law; the pan lane maps one to one | unchanged, now proven by a render test against Live's table | tests/LiveFacts190gxTests.inc |
| Envelopes tab: the envelope over the waveform, the Device / Control choosers at the bottom left, Linked toggle; Gain axis even in Live's value (the −1…1 staircase draws as even steps) | choosers on top; axis = the lane's 0…1 value | choosers at the bottom left; the Gain axis is straight in Live's value; the area under the line filled faintly | ClipAudioPanel.cpp ClipEnvelopeView |

Left out (listed): Live's "Linked" loop switch (LYNX's envelopes always follow the clip's loop; a switch with nothing
behind it would be a dead control), the other Clip controls of the chooser (Transposition, Transient Envelope, Grain
Size, Flux, Sample Offset, Pro Formants / Envelope — not modulated by LYNX), the % read-out on a breakpoint (LYNX shows
dB; Live's % meaning was not recorded).

## 2. Warp-mode settings in the Clip View (corrects round 190fm's placement)

Live's Audio box (`warp/live-audio-box-4-modes.png`): Warp, the mode chooser, then the mode's own rows — Beats: the
"Preserve" label, its chooser, then the icon-only loop-mode chooser beside the Transient Envelope box; Tones: "Grain
Size" + box; Texture: "Grain Size" + box, "Flux" + box; Complex Pro: "Formants" + box, "Envelope" + box (the chooser
reads "Pro"). LYNX now places them so: labels above full-width boxes, values with Live's two decimals, the loop-mode
chooser drawn with Live's three icons (its list adds each name beside the icon). Flux and Complex Pro's boxes appear
only when LYNX's engine plays them (`warpmodes::textureFluxPlays`, `complexProPlays`).

## 3. Best way?
- **FL:** no per-clip envelope (Automate › Volume / Panning makes automation clips: round 190dx); FL layout keeps that.
- **Ableton:** clip envelopes in the Clip View, warped clips only, gain law above (measured).
- **Users:** not researched this round (the brief was Live-exactness from the renders).
- **Verdict:** Live-exact in the Clip-launch layout and the .als; LYNX's own Playlist envelopes (FL layout) keep
  playing on unwarped clips (a LYNX-own extra, the owner's #24).

## 4. Beats law switch and the three voice cases (gate review)
- **Classic | Measured** (`WarpModeSettings::beatsLaw`): Measured = Live 12.4's measured Beats laws (new channels,
  .als / .flp imports); Classic = the Beats code before 190gx, kept verbatim. Every save writes the mark
  `beatsLaw190gx`; a project without it loads every channel Classic, and the warpModes list carries the law as a tenth
  number. Proof of bit-identity: FNV prints of 10 Beats settings (plain and warp-mapped) from the pre-190gx
  TimeStretch.cpp and from the Classic law are equal (`/mnt/e/lynx-work/livefacts190gx/law/`); als175a's exact-sample
  Beats tests run on both laws; `--selftest` beatslaw190gx_saved. A Classic channel shows a Classic | Measured chooser
  in the Clip View's Beats rows.
- **The three voice cells** that got slightly worse (r0.8 default, r0.8 Loop Off, r1.5 Envelope 50): with Live's own cut
  points forced into both laws, Measured beats Classic on every metric for r0.8 default and r1.5 Envelope 50 (an
  artefact of LYNX's level-based detector cutting elsewhere); r0.8 Loop Off keeps a real spectral cost (a piece started
  at once inside sustained sound). A sustain-aware crossfade halves it with Live's cuts but changes nothing with LYNX's
  own cuts and costs a little envelope deviation: not landed. Detail: `/mnt/e/lynx-work/livefacts190gx/law/VOICE-CASES.md`.
