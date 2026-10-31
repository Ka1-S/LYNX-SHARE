# REA round 167f (flgap167) — FL plugins and engines LYNX SHARE still lacks

Date: 2026-10-31. Gap table: [`REA_rodada167f-gaps.md`](REA_rodada167f-gaps.md). Clean-room: FL Studio's own plug-in
DLLs are studied with Ghidra (headless, one instance machine-wide, `-max-cpu 2`) and run unmodified in our own harness
(round 154's `fx.c`); this document describes their behaviour in our own words.

Markers: **[DECOMP]** Ghidra, **[EXEC]** the DLL in our harness, **[DATA]** project / preset files, **[HYP]** not verified.

## 1. Emphasis

(in progress)

## Where I stopped

- 2026-10-31: worktree `~/projetos/lynx-wt-flgap167` (feature/flgap167), data `/mnt/e/lynx-work/flgap167`, tools
  `~/.claude/jobs/f8789b49/tmp/flgap167`. Gap table written. Next: Ghidra on `Emphasis_x64.dll`.
