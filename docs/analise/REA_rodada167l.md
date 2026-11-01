# Round 167l (v0.1.22) — Sampler legacy effects, export extras, Alt solo, channel lock, CC 123 on reset

Clean-room: Ghidra on FL Studio 2026's own `FLEngine_x64.dll`, its DFM form resources and published field / method
tables; command-line renders by FL itself (`FL64.exe /R`, both FL locks held). Behaviour and formats only; nothing of
FL's code is copied.

Markers: **[DECOMP addr]** Ghidra pseudocode, **[DFM]** a form resource, **[DATA]** FL's demo projects, **[FL render]**
FL's command-line render, **[GUI]** LYNX driven on Windows (fresh profile, `LYNX_TEST_MODE=1`), **[TEST]** LYNX tests
(`tests/Rea167lTests.inc`).

(sections are filled as the items land)

## Where I stopped

- In progress: item 1 (Sampler legacy panel) — REA done (see §1 once written), FL test project
  `tmp/r167l/py/mk167l.py` → `C:\Users\kauan\lynx-test\r167l\fl\l_fx.flp` (not rendered by FL yet).
- REA notes so far: tmp/r167l/d*.c (decompiles), m*.txt (constants). Alt solo = FUN_0127c2a0 flags 3; channel lock
  0x20 = SoloLockMenuClick 0x1213320 → FUN_00ffac40; CC 123 = FUN_011c89c0 (outputs with "Send all notes off").
- Rerun: `tmp/r167l/g.sh <out.c> 0 <hex addr…>` (decompile), `SCRIPT=Mem.java|Dump.java|Off.java tmp/r167l/g.sh …`.
