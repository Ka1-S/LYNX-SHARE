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
| Add an effect to a mixer slot | fl-09: Fruity Parametric EQ 2 added to an empty slot opens its window | b1-21: slot filled, window closed | The added effect's window opens (all kinds, not only plugins); verified v-05 |
| House 124 sidechain: "Sidechain to this track", then add a compressor (as LYNX's box said) | FL not driven for this step. FL's Fruity Limiter / Fruity Compressor keep their own side-input number in their state (limiter v6+ at 0xA4, compressor v3+ at 32: REA round 48, core/FruityDynamics.cpp) — FL users sidechain with these | h-13: modal box; h-16: the compressor added afterwards had side input "None" — no ducking; `sidechainTo` only linked LYNX's Compressor and the vocoder, never Fruity Limiter / Compressor | Reminder in the hint bar; `mixerops::linkAddedSidechain`: a Compressor / Fruity Limiter / Fruity Compressor added to an insert with ONE sidechain sender (level-0 send) listens to it; `sidechainTo` also links Fruity Limiter / Compressor (`listensToSidechain`). A vocoder is not auto-linked (its side input is the modulator) |

## Logged for other rounds / decisions
See the tracker table: FX and instrument menus (170b #16), 
Ctrl+H Stop sound and Ctrl+N Save new version (user decisions), rack items (170a), toolbar/snap/zoom (170e).

## Round 170h2 (user decisions 2026-11-04, branch feature/wfqa170h2)

| Item | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| Ctrl+H | DFM `ShortcutsModule/MainMenuActions/MacroStopSoundAction` ShortCut 16456 (= Ctrl+H), Tools › Macros › Stop sound, toolbar StopSoundBtn hint "Ctrl+H". Handler: published method `MacroStopSoundActionExecute` (method table at file 0xb16110) → code 0xf1de20 → FUN_011c8d10: (a) a global double at 0x15e2cf0 set back to 1.0 when it is not (FUN_011c8be0, with a hint; likely a playback-speed multiplier, not confirmed); (b) FUN_011c89c0: for each MIDI output device, CC 123 (status 0xB0+ch, 0x7B = All notes off) on its 16 channels; (c) FUN_011c8a40 (1, 1, 1): every channel's voices killed (FUN_00fe8f70 per channel), every mixer track's state reset (FUN_0127b530), and mode 1 → FUN_012b5210 = every registered plugin's reset (vtable +0x48; the same reset FL's "Reset plugins on transport" uses, round 167k) | Ctrl+H = Undo history; Stop sound only in Tools › Macros, = the panic (notes off, no effect reset) | Ctrl+H = `tools.stopSound` → `AudioEngine::stopSound`: the panic (every instrument's all-notes-off, plugin generators reset with their wrapper option, MIDI outs cut) and then `RackRenderer::stopSoundChannel`: every effect's `transportReset` (native effects cleared, hosted effects reset with their wrapper option). (a) not mapped (its meaning is not confirmed). Undo history → Ctrl+Alt+H (free in FL's DFM shortcut list and in LYNX's KeyMap). Menu, Stop-button hint and F1 sheet follow the KeyMap |
| Default new project | fl-02: FL's template, 808 channels with empty steps | Kick / Clap / Hat steps pre-programmed | `makeNewProject()` = the "Empty" template: same channels, inserts, Master limiter, no steps ("Basic beat" keeps the beat; `Song::makeDefault` unchanged for the tests and the self-test) |
| Double-click an audio clip (lo-fi 85 session, l-05) | DFM `ShortcutEditorForm/BackPanel/AudioClipsPanel` "Double-click on audio clips": Show clip properties / Show channel settings, modifier (none) or Alt | nothing (hint: "[2xLMB] opens"); `openClipSource` had no audio branch | channel settings; Alt: clip properties |

## Where I stopped
- Done: LYNX trap session + FL session, tracker table, batch 1 + batch 2 (effect window opens on add) with tests.
  build-local 1963861 checks / 0 failures; Windows build OK.
- GUI-verified on the branch build (shots C:/Users/kauan/lynx-test/wfqa170h/shots/v-*): New dialog Enter creates and
  LYNX keeps the foreground (v-02); Esc closes Sawer (v-03/04) and Parametric EQ windows, foreground stays on LYNX;
  Parametric EQ added to Insert 6 opens its window (v-05); Piano roll Ctrl+A, Ctrl+B copies a note one whole bar later
  at the same offset (v-07); export ends with "Export complete: path" + "Peak: -3.0 dBFS" in the hint bar, no box,
  foreground on LYNX (v-10). Desktop clean after close (v-12).
- House 124 session (h-*): BPM, swing wheel, chord + stretch + Ctrl+B/transpose, sidechain, stems MP3, save/reopen.
  Batch 3 (sidechain in either order) built, tests 1963872 checks / 0 failures, GUI-verified s-02 (hint, no box)
  and s-03 ("Compressor <- Insert 1" right after adding it).
- In progress: merge train (tmp/r170h/train1.log, train.sh PID 1180345) — the queued feature/wfqa170h includes batch 3.
  04:55: still waiting; the batch ahead (land.sh gap169k-doc toxseq169h…) hangs in build-local since 03:29
  (6 zig c++ in futex wait on ~/.cache/zig-main, load 0). If wfqa170h comes back CONFLICT: merge origin/main
  (MixerComponent.cpp near showAddMenu / showSendMenu is the likely spot), rerun build-local, requeue.
- Next: lo-fi (85) session (audio loops from the Browser, time-stretch), Save/Open dialogs at the screen's top-left (logged S3).
- Rerun: `ZIG_GLOBAL_CACHE_DIR=~/.cache/zig-wfqa170h-local ZIG_LOCAL_CACHE_DIR=~/.cache/zig-wfqa170h-local coord/buildslot.sh scripts/build-local.sh`;
  `OUT=build-win-wfqa170h coord/winbuild.sh scripts/build-windows-zig.sh`; GUI helpers tmp/r170h/g.sh
  (driver key names are UPPERCASE, e.g. `key CTRL+B`; ABNT2 keyboard: `key BSL` = backslash, `key SHIFT+SLASH` = colon).
