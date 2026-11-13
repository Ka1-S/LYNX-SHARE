# Round 190ev: issue #152 follow-ups (0.1.26, Windows 11)

Two reports from the issue's thread: (1) "0.1.26 alpha shows flickering windows, and selecting e.g. the window behind
does not bring it over the one in front; the Channel rack almost never comes to the front"; (2) with a Focusrite Solo
(input 2 mono on mixer insert 7) the take lands on another insert (8 or 10), and soloing insert 7 is silent while
soloing 8 / 10 is heard.

## 1. Window stacking and flicker

### What 0.1.26 changed
Issue #116 (rounds 186a/188a/188j/190i) made FL's docked plugin windows the default: every floating panel (Playlist,
Channel rack, Piano roll, Mixer) and every third-party plugin editor became a native child window of the main window,
so the system keeps their stacking order and a clicked one comes in front (`ui/NativeChild.*`, `ui/Workspace.cpp`).

### Causes (base e5739ab8c, wip/rollup027)
1. **LYNX's own editor windows were left out of the docking.** Channel settings (`ChannelSettings.cpp` SettingsWindow),
   the native effect editors (`PluginEditors.cpp` NativeEffectWindow, `FacetEqEditor.cpp`, `ProEditors.cpp`,
   `NewtoneEditor.cpp`, `FlExtraEditors.cpp`, `VisualEditors.cpp`), the controllers (`Controllers164.cpp`,
   `ControllerEditors.cpp`) and the automation clip window (`AutomationUi.cpp`) still called `keepAboveMainWindow`: an
   *owned top-level* window, which Windows always keeps above its owner and therefore above every child window of it.
   Clicking the Channel rack raised it among the children, under those windows: it could never come in front of a
   channel's settings or an effect editor, whatever was clicked. In FL these are plugin windows like any other (the
   Sampler's channel settings float inside FL's workspace, `/mnt/e/lynx-work/dock188a/shots/116-5-rack-clicked.png`,
   REA_rodada186a / 188a: a docked window goes behind a clicked panel and comes back when clicked).
2. **A click inside a docked plugin's own view did nothing.** The frame raised itself only from JUCE mouse events
   (`PluginWindow.cpp` DockedPluginFrame::mouseDown); the plugin's editor is a native window of the plugin, so JUCE never
   hears of clicks in it. Only the title strip brought a plugin window to the front.
3. **Flicker on every focus change.** `WindowOwner.cpp` matchWindowOrderToScreen runs whenever the main window is
   "brought to front" (each activation: a click back into LYNX, a dialog or a detached window closing). It existed to
   fix JUCE's own list of desktop windows (drop targets, issue #12), but did it by calling `toFront` on every docked
   child and every owned window: one SetWindowPos per window, each child jumping to the top for a moment and being
   drawn again.
4. **Flicker when a docked panel opens or closes.** Round 190i fades a docked panel by its window's alpha. In JUCE 8.0.4
   (`juce_Windowing_windows.cpp` D2DRenderContext::updateConstantAlpha) an alpha below 1 replaces the Direct2D
   context with a layered-bitmap one and alpha 1 replaces it back, each new and empty until its first frame: the panel
   flashed at the start and at the end of every F5..F9 open / close with Animations on.

### Fixes
1. `dockOrKeepAbove` (`ui/WindowOwner.*`): the windows listed above dock in the workspace: LYNX's title bar (a child
   window has no native one) with the close button, attached with the window's own style flags (JUCE turns a
   TopLevelWindow back into a top-level window whenever it sees other flags), placed where its code put it (mapped
   into the workspace and kept inside), registered with the workspace like a docked plugin frame (re-hosted with the
   main window, stacked by the same rules), raised by any click in it. FL's "Detach all new plugins" (or
   `LYNX_DOCKED_PLUGINS=0`) keeps them floating as before. "Align all channel editors" now works with screen
   positions, so it aligns docked and floating editors alike.
2. `nativechild::raiseOnAnyClick` (`ui/NativeChild.*`): on Windows a WH_GETMESSAGE hook of the UI thread sees each
   button-down message; when it lands in a native window inside a docked frame (a plugin's view), the frame comes in
   front after the message (and runs the frame's "last used / linked modules" step). Clicks on the frame's own window
   still go through JUCE.
3. matchWindowOrderToScreen on Windows only reorders JUCE's list (`ComponentPeer::handleBroughtToFront`); no native
   window moves. The screen order is already right (the children were read from the system, owned windows are always
   above the owner). Other platforms keep the raises (no owner windows there).
4. On Windows a docked panel shows and hides at once (FL's docked windows don't fade); plain panels (docking off) and
   other platforms keep the fade (`dockstack::panelFades`).

The rules are in `core/DockStack.h` (tests `win190ev_*`); `--plugwintest` dock mode checks the docked editor, the
clicks (posted to LYNX's own windows, no system input) and the panels without fade.

### Not changed / approximate
- A docked LYNX editor has no minimise button (FL's docked strip has one: DockedPluginFrame collapses to its strip;
  a JUCE DocumentWindow's minimise would minimise a child window the system's way). No per-window "Detached" item
  for LYNX's own editors yet (FL has it in each plugin window's menu); the global option covers it.
- Tool windows and dialogs (note tools, history, export, settings) stay owned top-level windows, as FL's dialogs.

### Best way?
- **FL:** docked plugin and channel windows inside the workspace; any click brings a window in front (REA_rodada186a,
  188a, shots above).
- **Ableton:** plugin windows are separate floating windows above the main window (Options › Auto-Hide Plug-In
  Windows); no docked windows (REA_rodada188a).
- **Users:** FL users ask for plugin windows that don't get lost behind panels (REA_rodada188a, "detached by default"
  requests), which FL answers with "Detach all new plugins"; this report asks for the opposite (the window clicked
  comes in front).
- **Verdict:** FL-exact (docked, click brings to front) as the default, FL's option for floating windows.

## 2. Which insert plays a recorded take

### FL evidence (static REA of FLEngine_x64.dll, FL Studio 2026; snapshot /mnt/e/lynx-work/win190ev/flengine.snapshot)
The take recorder (`FUN_0126e690`) installs a finish callback, `FUN_012aa880`, which creates the take's audio clip
channel (type 4) and sets its mixer track (channel +0x258):
- insert X itself when a Playlist audio track of the current arrangement is linked to X (`FUN_012a76e0(X, 0)`), or when
  X's "Record" position (mixer[X]+0x2f, written by the menu handler `FUN_012aada0`) is External input only (0);
- otherwise (External and mixer input, Post effects, **Post EQ = FL 2026's default**, Post level and panning, Post
  track) the lowest-numbered insert X is routed to (route flags mixer[X]+0x398+8·dst, toggled by `FUN_012a33c0`,
  Python's `mixer.setRouteTo`; Master = 0 first), Master when X goes nowhere.
FL never gives the clip a new free insert ("Route to free mixer track" is a separate, manual action). This also
settles the online manual's two sentences ("routed back to the same Mixer Track it was recorded from" = audio tracks /
External input only; "routed to the same Mixer Track the Input Mixer Track is routed to" = a plain insert with the
default pickup): the take already holds X's processing, so X does not process it a second time.
Details and the field map: /mnt/e/lynx-work/win190ev/REA-recorded-clip-routing.md.

### LYNX before (rollup027) and the report
Round 168x let a take tapped after the slots play through a **new free insert** (`AudioEngine.cpp` startAudioTake,
"LYNX's choice, FL's routing was not measured"). With FL's default Post EQ that is every take: the reporter's input 2
on insert 7 gave clips on new inserts 8, 10…; soloing 7 left those inserts out (`Song::soloAudible`), so the take
was silent, while soloing 8 / 10 played it.

### Fix
`takes::playbackInsert (song, X, position)` (`core/TakeRecording.*`) is FL's rule (LYNX's Playlist tracks are shared by
all arrangements, so any audio track linked to X counts). The engine uses it for every take; the take's Playlist row
is chosen by the recording insert (its audio track first, as FL's `FUN_012aa4d0`), and the row is linked to the insert
only when the clip plays through it (a take on Master leaves its row as it was). Takes of no insert (Settings' "Record
input audio" without armed inserts) still get a new insert. Crash journals save the insert as `playsThrough`
(Master = 0, -1 = new) and still read the older `insert` key (0 = new).
With insert 7 soloed, the Master (where the take now plays) and insert 7 (its live input) are audible: tests
`win190ev_a_take_plays_where_fl_routes_it`, `win190ev_a_take_on_master_leaves_its_track_unlinked`,
`win190ev_crash_journals_keep_master_and_read_old_files`,
`win190ev_soloing_the_recording_insert_keeps_its_input_and_take_audible`.

### Needs a decision / live check
- **Input monitoring default.** FL's fresh insert hints "Recording: Post EQ, Monitoring: When armed"
  (REA_rodada165-issue11c shot); LYNX's default is "Auto" (round 88, a model: like When armed but silent during
  playback without recording). With insert 7 armed and soloed during plain playback, FL plays the input, LYNX does
  not. Recommended: FL's "When armed" as the default for new inserts (projects keep what they saved). Not changed in
  this round.
- A live check of the rule in FL (record Post EQ on an insert routed to a bus) would confirm the static reading.
