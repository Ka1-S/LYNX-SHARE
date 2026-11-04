# Round 170h — end-to-end "make a beat" workflow QA (v0.1.24)

Source: the pro producer's core message (Busy Works Beats, 2026-11-03, 23:24): "go through the entire workflow over and
over and find where you get stuck". The full friction log (every step, FL vs LYNX, severity, owner round) is
`/mnt/e/lynx-work/tracker/workflow-qa-0.1.24.md`. Method: LYNX main 2c313486 (Windows portable, fresh
LYNX_PROFILE_DIR + LYNX_TEST_MODE=1) driven with real input (SendInput keys with scan codes, mouse_event clicks, drags,
wheel), then ONE FL Studio 2026 session (both locks, closed by its PID, desktop checked clean) for the steps where FL's
behaviour was not proven. Screens: `C:\Users\kauan\lynx-test\wfqa170h\shots` (b1-* LYNX trap beat, b2-* LYNX house
start, fl-* FL). FL shortcut list: FLEngine DFM dump `/mnt/e/lynx-work/r167j/dfm-all.txt` (decoded by tmp/r170h/sc.py).

User direction (2026-11-04): match FL where an FL user would get stuck or lost; keep LYNX's own design when it works.

## Fixed (per item: reporter's words | FL evidence | LYNX before | LYNX now)

| Item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| "find where you get stuck": after closing a dialog the keys did nothing | fl-06: after Esc closes a plugin window FL's main form is the foreground window | b2-13b: File › New › Create, then F6: nothing; the foreground window was the terminal (another program). Every tool window (New project, note tools, history, quick-add, tempo tapper...) and the other owned windows did it; only plugin windows had the round 165q fix | `ToolWindow::closeSoon` and every window kept above the main window (`OwnershipKeeper`: hidden or deleted while active) hand the activation to the main window first |
| Esc on a plugin / channel window | fl-02→fl-03 (808 HiHat channel window), fl-05→fl-06 (Sawer), Fruity Parametric EQ 2: Esc closes the window | b1-06/07: Esc did nothing on Sawer's window | An Esc nothing in the window used closes it (`closeOnEscape` in the secondary-window key forwarder; text fields and editors that use Esc keep it; detached panels unchanged) |
| Piano roll Ctrl+B | fl-21/22: notes over steps 1-8, Ctrl+B → copy at bar 2; fl-23: notes over steps 1-24 → copy at bar 3 (snap on Step) | b1-11: copy right after the last note (a riff ending at step 10 repeated from step 11) | `noteedit::duplicateBars`: the span rounded up to whole bars (the time signature's bar) |
| Export: "Export complete" box | fl-26: the render window closes, nothing to click | b1-28: modal box with path and peak | Path, info and peak in the hint bar for 8 s; a box only for a notice that needs reading (and when nothing was exported) |
| New project dialog | (FL has no New dialog; its dialogs take Enter as OK) | Enter did nothing in it and reached the main window (maximised the Playlist behind it); double-click on a template did nothing | Enter (list or a text field) and a double-click on a template = Create; the template list has the focus when it opens |

## Logged for other rounds / decisions
See the tracker table: FX and instrument menus (170b #16), add-effect opens its window (170h batch 2, after 170b),
Ctrl+H Stop sound and Ctrl+N Save new version (user decisions), rack items (170a), toolbar/snap/zoom (170e).

## Where I stopped
- Done: LYNX trap session + FL session, tracker table, batch 1 code + tests (tests/Wfqa170hTests.inc).
- In progress: build-local + Windows build of feature/wfqa170h, then GUI verification on Windows, then train.
- Next: batch 2 (effect window opens when added to a slot) after 170b lands; house (124) and lo-fi (85) sessions:
  audio loops from the Browser, stems / MP3 export, swing, sidechain compressor set-up, save/reopen.
- Rerun: `ZIG_GLOBAL_CACHE_DIR=~/.cache/zig-wfqa170h-local ZIG_LOCAL_CACHE_DIR=~/.cache/zig-wfqa170h-local coord/buildslot.sh scripts/build-local.sh`;
  `OUT=build-win-wfqa170h coord/winbuild.sh scripts/build-windows-zig.sh`; GUI helpers tmp/r170h/g.sh.
