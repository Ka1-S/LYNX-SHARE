# REA round 167b — v0.1.22: Newtime, Newtone, Transmitter parity

Worktree `../lynx-wt-ntt167`, branch `feature/ntt167` (wip: `origin/wip/ntt167`). Clean-room as every REA round:
behaviour in our own words; FL's own front ends (Delphi DLLs) may be decompiled; zplane's engine
(`Newtone_Engine_x64.dll`, licensed) is black-box only (EXEC renders of FL's unmodified DLL, as round 165g).
Data: `/mnt/e/lynx-work/ntt167`.

Evidence markers: [DECOMP addr], [EXEC], [SHOT], [TEST], [HYP].

## Where I stopped
- 2026-10-31: worktree created, briefs and earlier rounds (73, 95, 165g flnewt165 / fltrans165) read. Starting with
  Transmitter (sends, steps:ticks, envelope view), then Newtone, then Newtime.
