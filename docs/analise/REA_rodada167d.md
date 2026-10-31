# REA round 167d — Kepler Exo on FL's own engine (v0.1.22)

Goal: Kepler Exo (FL 2026) played by LYNX on Kepler's engine port, clean-room from REA on FL's own
`Plugins/Fruity/Generators/Kepler Exo/engine_x64.dll` (SHA-256 `10ed2464…6d25c`), bit-exact or as close as measured;
"Kepler-2" presets / .flp import; editor in LYNX style. Data: `/mnt/e/lynx-work/exo167` (`xeng/` 188 factory engine
states, `run/kp.exe` EXEC harness from round 165g, `gh/exo_all.c` full Ghidra decompile of the Exo engine).

## Findings so far
- The Exo engine's per-sample render `FUN_18001b7d0` decompiles to the same code as Kepler's `FUN_18001b940`
  (normalised diff: identical except addresses) [DECOMP]: Kepler-1 is the same engine with the Exo features off.
- The EXEC harness drives the Exo engine DLL unmodified (default state, 2 s, 0.2 s wall) [EXEC].

## Where I stopped
- 2026-10-31: worktree `lynx-wt-exo167` (feature/exo167) from origin/main 72339935; round doc started; reading the
  decompile for the Kepler-2 loader / panel map and the Exo-only DSP blocks.
