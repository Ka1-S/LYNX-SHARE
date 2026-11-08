# Round 182e: the Patcher's side-chain input pins on FL import (issue #112)

Evidence: FL's own data (the FL Studio 2026 demo projects) read with LYNX's FLP and Patcher parsers (probe output in
`/mnt/e/lynx-work/flp182e/pins.txt`), and FLEngine's input list from round 22. Clean-room: behaviour only.

- A Patcher I/O pin that brings audio in (group 1400, flags bit 0) has a **bus**: 0 = the slot's own input, k ≥ 1 = a
  side-chain input.
- FL gives a slot plugin its inputs as one list per insert: **[the insert itself, then every track routed to it, in
  ascending order (501 last)]**; a plugin asks the host for buffer k of that list and for its name
  (`REA_rodada22` §1.2 [DECOMP]: `FUN_012528f0`, host slot 36, dispatcher `0x1321053`). The Patcher's bus k is that k.
- **[DATA]** The only two side-chain pins in FL's demos, both in "Astes - Bien Duro":
  - insert 8, pin "7 Sidechain", bus 1: inputs `[8, 81]`, so k = 1 → track 81;
  - insert 17, pin "7 Sidechain", bus 4: 5 inputs, so k = 4 → track 81.
  Track 81 is **named "7 Sidechain"**. The pin's name is the source track's name when the file was saved, not an insert
  number. Issue #112's pin is named "Master" and its insert has a route into it.
- LYNX before: `atoi (pin name)` → insert 7 in Bien Duro (wrong) and no source for "Master" (issue #112).
  LYNX now: `sidechainSource (insert, bus)`, the same rule as the Limiter, Vocodex and the other side-chain effects.
  A bus past the list gets no source (FL's host returns no buffer for it).

## Best way?

- **FL:** the pin's bus is an index into the insert's input list (above). The name is only a label.
- **Ableton:** a side-chain source is a routing target with the source's stable track id (`AudioOut/Track.<Id>/…`,
  `/mnt/e/lynx-work/ableton/ALS-FORMAT.md`), so reordering tracks keeps it.
- **Users:** FL users complain that "a Patcher sidechain silently moves when tracks are reordered"
  (`/mnt/e/lynx-work/ableton/research-web-2.md` #4). That fits an index into the routed-tracks list. The reporter
  expects the routed insert to be the source.
- **Verdict:** FL-exact for the import (it has to read FL's index). LYNX keeps the resolved track number in the slot,
  so after the import its side-chain no longer moves when tracks are reordered, as in Live. Nothing in the UI changes.
