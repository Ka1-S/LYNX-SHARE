# Round 182e: Peak Controller outputs in an imported Patcher (issue #111)

- **FL's order [DECOMP, REA_rodada17 §5.1]:** the Peak Controller names its control outputs from the table
  `{"Peak+LFO", "Peak", "LFO"}` (`FUN_006fd830`, the "output control name" dispatch), and its tick computes them in
  that order: output 0 = Peak + LFO (sum), 1 = Peak, 2 = LFO. The DLL in FL Studio 2026 today
  (`Fruity Peak Controller_x64.dll`, SHA-256 `e73ec8dacb16d4db24444caa6d2d9db0a4b79246693288db4fa0972b63ec2d27`) is the
  one round 17 decompiled, byte for byte. The UTF-16 strings "Peak+LFO", "Peak", "LFO" sit next to each other in that
  order in it.
- **The Patcher's port k is the plugin's output k [DECOMP `Patcher_x64.dll`, round 182e, outputs in
  `/mnt/e/lynx-work/flp182e/dec/fs10.c`]:**
  - `FUN_007ff430` (re)builds a node's control outputs: for every k below the hosted plugin's output-controller count
    (wrapper `+0x710`), it asks the plugin for output k's name (wrapper vtable `+0x238`) and creates the port with id
    `0x10000000 + k` (`FUN_007f8c50`: `base + (k & 0x0FFFFFFF)`) under that name.
  - `FUN_00801bf0`, the wrapper's "controller changed" callback, sends the value to port `0x10000000 + index`.
  - So the number is the one the plugin itself uses for its outputs. A mixer link's `0x8000 + k` (REA_rodada14 §2) is
    the same k, and the mixer-link import already passes k through unchanged.
- LYNX's Peak Controller has the same order (0 peak + LFO, 1 peak, 2 LFO; REA_rodada141 §2).
- LYNX before: the Patcher import remapped 0/1/2 to 1/2/0, an order from round 14's guess ("peak, LFO, product"),
  which round 17 later corrected. Issue #111's wires come from FL output 1 (Peak). They landed on LYNX's LFO output,
  which is constant with the LFO amount at 0, so the driven EQ bands stayed at their minimum.
- LYNX now: k is kept, as for the mixer links.

## Best way?

- **FL:** output k = the table above, the same numbers in the Patcher and in mixer links.
- **Ableton:** no Peak Controller with numbered outputs: an Envelope Follower (Max for Live) maps one output to one
  target. It has nothing to say on the numbering.
- **Users:** the reporter measured FL within 0.2 dB with the identity mapping.
- **Verdict:** FL-exact. No UI change.
