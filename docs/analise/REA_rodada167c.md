# REA round 167c — Edison (Deverb, Convert to score, Run script) and Scratcher DVS / RIAA (v0.1.22)

Evidence markers: [DECOMP addr] Ghidra on FL Studio 2026's own binaries (FLEngine_x64.dll, Edison_x64.dll,
Fruity Scratcher_x64.dll, its engine_x64.dll), [DFM] form resources, [REF] FL's shipped reference text, [SHOT] driven FL /
LYNX screenshot, [TEST]. Clean-room: behaviour only, implemented from this document.

## Where I stopped
- 2026-10-31 12:45: worktree lynx-wt-edscr167 (feature/edscr167) from origin/main 72339935; work data on
  E:\lynx-work\edscr167 (dec.sh = headless Ghidra, read-only). Starting REA: Scratcher engine_x64.dll (DVS), then
  FLEngine (fle.dll project in D:\lynx-work\rea165) for Deverb / Convert to score / Run script.
