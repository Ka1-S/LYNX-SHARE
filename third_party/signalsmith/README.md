# Signalsmith Stretch + Signalsmith Linear (vendored)

Used by `src/core/TimeStretch.cpp` for pitch-preserving time-stretch (audio clips that follow the tempo, Newtone,
the audio editor).

- Signalsmith Stretch — https://github.com/Signalsmith-Audio/signalsmith-stretch — commit 88061fd (1.4.0 + Linear
  bump), file `signalsmith-stretch.h`. MIT licence, see LICENSE-stretch.txt.
- Signalsmith Linear — https://github.com/Signalsmith-Audio/linear — tag 0.6.6 (7bb1c8f), files `linear.h`,
  `fft.h`, `stft.h`, `approx.h` (no platform back-ends: the built-in FFT is used). MIT licence, see
  LICENSE-linear.txt.

`signalsmith-stretch.h` includes `"signalsmith-linear/stft.h"`, which resolves next to it.

Modified by LYNX (round 190kr, performance; MIT notices kept): the stretcher's per-band state is structure-of-arrays,
its spectral loops and the FFT's split-complex passes / real-FFT twists run four values at a time with SSE2 (a scalar
fallback elsewhere), the main prediction computes everything off its band-to-band chain first, a mono source's second
analysis is copied, a channel that is never the loudest skips its unread preliminary outputs, and the output is read
in runs. Every value is computed by the same IEEE operations in the same order, so the output is bit-identical to the
unmodified copies (tests/Perf190krTests.inc pins it). Changes are marked `LYNX (round 190kr)`.
