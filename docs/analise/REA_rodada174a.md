# Round 174a — issue #32 (video YMfABy9RE9Q + crash reports)

Target: 0.1.25. Branch `wip/issue32e` (base origin/main 4857f27d).

## Video
- Music only, no speech (the transcript is empty). Frames show no LYNX bugs.
- Cosmetic: narrow third-party editors sit in a 480 px wrapper (`minWidth` in
  `PluginWindow.cpp`). Low priority. Needs FL proof before any change.

## Crash reports (7 from the reporter, symbolicated with the 0.1.23 CI PDB)
| Report | What | Where |
|---|---|---|
| 5346b94e | access violation | inside a third-party synth's own window procedure, under `DispatchMessageW` |
| b2e3b3ee | heap corruption (0xC0000374) detected | `MixerTrack::~MixerTrack+0x91` <- `Song::~Song` <- `RackDisposer::handleAsyncUpdate` |
| e31162a7 | heap corruption detected | `ASIOAudioIODevice::~ASIOAudioIODevice+0x114`, at shutdown |
| others | unclean exit | no stack |

The two heap corruptions are only where Windows noticed the damage. The writer
is most likely a plugin. The machine is an i5-8350U running the other DAW's ASIO
driver at a 4096-sample buffer.

Earlier thread items were fixed in 0.1.23. The playlist cursor and brush fixes
(issue32d, a09d6c35 / merge 1b616474) ship in 0.1.24.

## Phase 1 (62ee7376)
- A WER/heap-failure "hang" file now counts as a crash
  (`crashreport::hangIsFatalReport`).
- `describe(0xC0000374)` returns "heap corruption".
- Nearest-export names are no longer shown for LYNX.exe frames (they were
  misleading).

## D1: catch faults inside plugin editor windows (user decision: yes)
Windows SEH only.
- **JUCE patch** (`cmake/LynxPatchJuce.cmake` item 15, `juce_Messaging_windows.cpp`):
  - adds `juce::lynxDispatchMessageHook`;
  - `dispatchNextMessage` calls the hook instead of `DispatchMessage` when the
    hook is set.
  - `build-windows-zig.sh` now copies and patches `juce_events` too.
- **`crashguard::dispatchGuarded`** runs `DispatchMessageW` inside `__try/__except`.
  - The filter unwinds the stack and takes the first module that is not a
    system DLL:
    - LYNX.exe: the fault stays a crash (we never hide our own bugs);
    - a plugin module: the fault is offered to the window claim.
  - Codes that are never caught (`editorFaultCatchable`): breakpoints, heap
    corruption, stack overflow, fail-fast/GS, assertion, thread-name and debug
    print. After those the process is not safe to continue.
- **Claim** (`PluginWindow.cpp`):
  - walks GA_ROOT plus the GW_OWNER chain up to a PluginWindow. GA_ROOTOWNER
    cannot be used, because plugin windows are owned by the main window;
  - checks the fault module against the plugin's file or bundle folder;
  - thread messages (no hwnd) are matched by module only.
- **On a fault**:
  - the root window is hidden and added to a dead list, and later messages to
    it are dropped (`ValidateRect` on WM_PAINT);
  - the trace is written to `autosave/editor-crash <stamp>.txt`;
  - the session note `plugin editor crashed: <name> (...)` is added, so a
    later crash report links to it (`CrashReporter` uses the trace file when
    there is no crash file);
  - a line is appended to `plugin-crashes.log`;
  - the window is deleted asynchronously inside `crashguard::run`. If deleting
    faults, it is leaked.
- **Dialog** (MainComponent timer): "{0} crashed. Save your project now (as a
  new file is safest)." Buttons:
  - Save as...;
  - Always run {0} in a separate process: persists `setPluginAlwaysSeparate`
    and switches matching channels and mixer slots to a separate process;
  - Close.
- **Start-up**: `uncleanCause()` falls back to the newest editor-crash step, so
  the unclean-exit dialog also offers the separate process.

### Limits
- Faults in messages dispatched by the kernel (sent messages, callbacks that
  do not go through our `DispatchMessage`) are not caught. They crash as before.
- Heap corruption is not caught: the damage is already done.
- The plugin's DSP may still be in a broken state. The dialog tells the user
  to save as a new file.

### macOS / Linux: not done
There, a fault is a signal (SIGSEGV/SIGBUS). Leaving the Cocoa or X11 event
dispatch with siglongjmp skips Objective-C/C++ unwinding and leaves locks held
by AppKit or Xlib. The process would go on in an undefined state, which is
worse than a clean crash report. Users there can run the plugin in a separate
process instead (the existing sandbox option).

### Tests
- `report174a_fatal_hang_is_crash`, `report174a_editor_crash_step` (parsing a
  name with parentheses, catchable codes).
- Core mini harness: 689 checks, 0 failures.
- `tools/testplugin/lynx_test_crash.c` mode `editor`: a CLAP gui with a Win32
  child window that writes through a null pointer 300 ms after opening.
- `LYNX.exe --editorcrashtest "<LYNX Test Crash.clap>" r.txt`:
  - opens the editor and expects exactly one caught fault, the window closed,
    a trace that names the plugin, and a message loop that still runs;
  - prints `result=OK`. Non-Windows prints `result=SKIPPED`.
- **Pending (release freeze)**: build-local tests, the Windows build
  `--selftest` and `--editorcrashtest`, after the 0.1.24 tag.

## Menu path (current main)
- Channel Rack: right-click a channel → Processing › "Run in a separate process"
  / "Always run {name} in a separate process".
- Mixer: right-click an FX slot → Processing › same items.
- The plugin window's Wrapper settings tab (Processing page).
- Global: Options › General › "Run plugins in a separate process (protects
  LYNX SHARE from crashes)".
