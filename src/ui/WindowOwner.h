#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class Workspace;

// Secondary windows (plugin, channel settings, EQ, automation) always stay above the LYNX main window,
// without staying above other programs: on Windows, the main window becomes their "owner".
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // call after the window is visible; sets the LYNX icon
// (it stays owned when JUCE recreates the native window later, e.g. setResizable after it is shown)
// The main window's native handle (HWND / X11 Window id; nullptr without one): a plugin's window in a separate
// process is kept above it (round 162d).
void* mainWindowHandle();
// The main window's component (nullptr before it exists: --snapshot routes, tests). Round 190jp: Settings › Theme takes
// a preset's thumbnail from it.
juce::Component* mainWindowComponent();

// Round 190ev (issue #152, 0.1.26: "the Channel rack almost never comes to the front"): LYNX's own plugin-like editor
// windows (channel settings, the native effects' and controllers' editors, automation clip settings) are FL's docked
// plugin windows: child windows of the main window floating in the workspace among the docked panels and plugin
// windows (issue #116), so a click on a panel brings it in front of them and a click on one of them brings it back. As
// owned top-level windows they stayed above every panel whatever was clicked. They get LYNX's own title bar (a child
// window has no native one) with its close button. `setEditorDock` names the workspace (nullptr: none, at shutdown);
// FL's "Detach all new plugins" (`setEditorsDetached`) or LYNX_DOCKED_PLUGINS=0 keep them owned top-level windows.
void setEditorDock (Workspace* workspace);
// Hotfix 0.1.31.1 (round 190qp): the workspace docked editor windows live in (nullptr: none) — ui/WindowPlaces.h keeps a
// docked window's place in its coordinates
juce::Component* editorDockArea();
void setEditorsDetached (bool detached);
// Call where keepAboveMainWindow was called (after the window is visible): docks the window, or keeps it above the main
// window when it can't be docked. True when it is docked.
bool dockOrKeepAbove (juce::DocumentWindow& window);
// Round 190jy (queue item 44; Settings › General › "Draw docked LYNX editors in the main window", off by default;
// LYNX_EDITORS_IN_MAIN=1 / 0 overrides it): a docked LYNX editor window (not a hosted plugin's, nor one that holds a
// native view of its own) is drawn by the main window as one of its components instead of a native child window, so a
// live display (Gross Beat, Fruity Limiter, Maximus...) costs its paint and no second window presenting frames. It keeps
// its place among the docked panels and plugin windows (Workspace::addEditorWindow). Windows only. Floating (detached)
// editors stay windows of their own. Switching it moves the docked editors that are open.
bool editorsInMainWindow();
bool editorsInMainWindowSetting(); // (the saved choice, without the environment's override)
void setEditorsInMainWindow (bool on);
// Moves the open docked LYNX editors to (true) or from (false) the main window's drawing, at the same place, in the same
// stacking position; returns how many moved. (The setting's switch; --uiperf A/B and tests.)
int moveDockedEditors (bool intoMainWindow);
// `window` is a docked LYNX editor drawn by the main window
bool isEditorInMainWindow (const juce::Component& window);
// The docked LYNX editor windows that are open (native child windows and the ones drawn by the main window)
std::vector<juce::DocumentWindow*> dockedEditorWindows();
// The window a component belongs to as the user sees it: the nearest editor drawn by the main window, else its
// top-level component (focus, linked windows, click raise)
juce::Component* editorWindowOf (juce::Component* c);
// A window's top-left on the screen, and moving it there, docked or not (a docked window's own position is in the main
// window's client area).
void setWindowScreenPosition (juce::Component& window, juce::Point<int> screenTopLeft);

// Issue #191 (round 190mx): a docked editor that a mouse press in a panel opens or brings in front (the Clip-launch
// Device View's window button acts on the press, as Live's does) stays in front of that panel. The panel raises itself
// for the same press after the press's own handler has run (its frame hears the press after its contents), so the new
// window ended up behind the Playlist. Docked editors call noteEditorRaised whenever they come in front; a panel's press
// handler calls keepPressRaisedEditorsInFront right after it raised the panel: the windows raised during that same press
// go back in front. A raise outside a press (keys, menus, a mouse-up) is not noted. LYNX_FIX191=0 turns it off (A/B).
void noteEditorRaised (juce::Component& window);
void keepPressRaisedEditorsInFront (const juce::MouseEvent& press);
// Issue #191: true when the action that is opening an editor now comes from a floating (detached) panel window: the
// mouse was pressed in one less than a second ago, or, without a recent press, the keyboard focus is in one. A docked
// editor would open inside the main window, behind that window; the Clip-launch and Simple layouts open it as a window
// of its own then (kept above the main window like the panel). Detached panel windows carry the property "lynxPanel".
bool openedFromFloatingPanel();

// Keys a secondary window does not use (no component in it took them) go to the main window's global shortcuts, as in
// FL (round 165q / issue #12: Space, F12 or a user's key did nothing while the Undo history, Settings, a plugin window...
// had the focus). keepAboveMainWindow installs the forwarding on the window; the main window sets the handler. A window
// that handles every key itself (a detached panel) sets the property "lynxOwnKeys".
void setSecondaryWindowKeyHandler (std::function<bool (const juce::KeyPress&)> handler);
// Issue #179: a modal dialog (an alert, Rename..., Export...) is open: the app is blocked as FL is, so keys a window leaves
// unused run no shortcut meanwhile.
bool modalDialogOpen();

// FL's "Auto select linked modules" (round 187a): a window kept above the main window that belongs to a channel or a
// mixer insert says so with markLinkedWindow; when one of them is focused (activated, or a control in it gets the
// keyboard focus), `handler` gets its channel (-1: none) and insert (-1: none).
void markLinkedWindow (juce::Component& window, int channel, int insert);
void setLinkedWindowFocusHandler (std::function<void (int channel, int insert)> handler);
void linkedWindowFocused (juce::Component& window); // (windows whose focus JUCE doesn't see: a plugin's native view)
// Runs that handler for a key from elsewhere (a separate-process plugin's window, issue #99); false when none ran.
bool runSecondaryWindowKey (const juce::KeyPress& key);

// Plain keys (letters, digits, punctuation) with the focus in a hosted plugin's own view (round 181c, issue #96). As in
// FL, a key the plugin leaves unused goes on to the typing keyboard: `left` gets what comes back from the plugin's view
// (listenForKeysPluginLeaves on its editor). The plugin gets the key first, unless `allows` says that window's plugin
// lets the host take them first (FL's per-plugin "Typing keyboard priority" button off; Windows): then `first` gets it before the plugin, and what it
// takes never reaches the plugin, repeats and key-up included. A native text field in the plugin always keeps its keys.
void setPluginViewPlainKeyHandler (std::function<bool (juce::Component& window)> allows, std::function<bool (const juce::KeyPress&)> first,
                                   std::function<bool (const juce::KeyPress&)> left);
void listenForKeysPluginLeaves (juce::Component& pluginEditor);
// Hotfix 0.1.31.2 (round 190ra; 10.net): a docked plugin frame (issue #116) hands the keys its plugin leaves unused to the
// typing keyboard and the shortcuts as a plugin window of its own does (keepAboveMainWindow's key listener): docked, they
// went to the shortcuts only, and the typing keyboard played nothing over a docked plugin.
void forwardUnusedPluginKeys (juce::Component& window);
// Tests (hotfix 0.1.31.2, round 190ra; Windows, elsewhere false / ""): the plugin's own native view inside `window`
// (its deepest visible child window) takes the system focus and gets `vk` pressed and released as posted keyboard
// messages (the path real keys take through the thread's hook); and the window the hook sees the focus in ("none").
bool postKeyToPluginViewForTest (juce::Component& window, int vk);
juce::String focusedPluginViewForTest();

// Before a drag (a sample from the Browser...): JUCE finds the drop target in its own list of windows, where the main
// window comes first after any click in it, while the windows kept above it (channel settings, a plugin...) are on top
// on the screen. This puts those windows back above the main window in that list, in their screen order, so a drop on
// one of them lands there and not on the panel under it (issue #12: a sample dropped on the Sampler's waveform
// replaced the sample of the Channel Rack row under the window). Runs whenever the main window comes to the front
// (setMainWindowForOwnership listens), so also when the drag image hides at the drop and Windows activates the main window.
void matchWindowOrderToScreen();

// Makes the main window the active one (round 165q): a closing secondary window that is active calls it first, so
// the activation does not go to another program.
void activateMainWindow();

// True when `window` is currently owned by the main window (Windows; elsewhere: keepAboveMainWindow was called).
bool isOwnedByMainWindow (juce::Component& window);
// True when `window` is above the main window in the z-order (Windows; elsewhere: isOwnedByMainWindow).
bool isAboveMainWindow (juce::Component& window);

// Keeps a top-level window reachable (round 163, UI zoom 150%+): a resizable window larger than its screen shrinks
// to it, and the window moves so its title bar and close button are on the screen. keepAboveMainWindow does it the
// first time; the UI scale change does it for every LYNX window.
void fitWindowOnScreen (juce::Component& window);
void fitAllWindowsOnScreen();

// Round 164: the content of a fixed-size window (a tool window, a dialog) that is taller or wider than the usable
// screen (UI zoom 150%+, a small laptop screen) goes into a scrolling viewport that fits, so its buttons stay
// reachable; a content that fits is returned as it is. The viewport follows the content's later size changes.
std::unique_ptr<juce::Component> fitContentToScreen (std::unique_ptr<juce::Component> content);

// The PC scan code (set 1, core/KeyMap.h typingpiano::usKeyOfScanCode) of the key that gives JUCE's key code
// `juceKeyCode` in the keyboard layout in use now; 0 when it is not a character key or the place can't be told. JUCE's
// key codes are the key's unshifted character ('Y' for the key left of X on a German keyboard): the typing keyboard
// asks for the key's place with this (FL goes by scan code).
int scanCodeOfKeyCode (int juceKeyCode);

// --windowtest only: makes `window` the foreground window even when LYNX was started in the background.
void forceForegroundForTest (juce::Component& window);

// Native window chrome in the LYNX colours (dark title bar on Windows). keepAboveMainWindow calls it.
void applyWindowChrome (juce::Component& window);

// GPU rendering (round 157a). JUCE 8 draws every Windows window with Direct2D (the GPU driver's D3D11 path); a
// driver that crashes there (Intel igd10um64xe.dll on a hybrid-graphics laptop) takes LYNX down. Off = every LYNX
// window draws with GDI (the CPU), as JUCE 7 did. Saved in gpu-rendering.txt ("0" = off; default on). The change
// applies to open windows at once (a watcher re-applies it to new windows twice a second).
bool gpuRenderingEnabled();
void setGpuRenderingEnabled (bool on);
// starts the watcher (the app calls it once at start, after reading the setting)
void startRenderingWatch();
// the window's native peer uses the chosen renderer now (keepAboveMainWindow calls it)
void applyRenderingChoice (juce::Component& window);

// Dialogs with a text field (rename and color, the next empty pattern's name, preset and insert names...) open with
// the field focused and its text selected, so typing replaces the name at once, as in FL (round 165q). JUCE's
// AlertWindow gives its buttons an explicit focus order, so the dialog's focus went to its first button: the letters
// typed did nothing and Enter pressed OK. Installed once for the whole app (a Desktop focus listener): the first time
// the focus enters a dialog, it moves to the dialog's first editable text field when it landed anywhere else.
void installDialogTextFocus();
void removeDialogTextFocus();
// The dialog's first editable, visible text field (top to bottom, then left to right), or nullptr.
juce::TextEditor* firstTextField (juce::Component& dialog);

// The LYNX SHARE logo by w3teal as the app icon (the mark on its dark tile; 512 px, with transparency), and the icon's
// small-size form (thicker lines, the mark larger in the tile: tools/make_logo.py) with the pepper stem for 21-32
// physical pixels and without it for 20 px and less. All three are square with the tile filling them.
juce::Image logoImage();
// The app icon decoded now, on the calling thread, outside the image cache (round 190hd: the splash's own thread)
juce::Image decodeLogoImage();
juce::Image logoSmallImage();
juce::Image logoTinyImage();
// The mark alone (w3teal's lynx in the red ring with the stem, no tile; 512 px) and its small-size form (128 px).
juce::Image logoMarkImage();
juce::Image logoMarkSmallImage();
// Where the mark's ring sits in the mark pictures (the ears and the stem rise above it): centre and radius as
// fractions of the picture's side.
struct LogoDisc { float x, y, r; };
LogoDisc logoDisc();
// The app icon at px x px, scaled down in halving steps (one big jump from 512 px aliases) and cached; the small-size
// forms up to 32 px. For window title bars and other small places.
juce::Image logoAt (int px);

// The CPU time this process has used so far, in seconds (user + kernel; --editorcpu).
double processCpuSeconds();
// The CPU time the calling thread has used so far, in seconds (user + kernel; --uiperf: the message thread's share).
double threadCpuSeconds();
// Round 190ih (--uiperf with LYNX_UIPERF_THREADS=1): every thread of this process with its name (Windows 10+: the
// thread description, else empty) and the CPU seconds it has used so far; empty where the platform can't list them.
struct ThreadCpu
{
    unsigned id = 0;
    std::string name;
    double seconds = 0.0;
    std::uint64_t cycles = 0; // (round 190kb)
};
std::vector<ThreadCpu> threadCpuList();
// Round 190os: SetThreadPriority(priority) on this process's threads whose name contains one of `nameParts` (and on the
// calling thread with `messageThread`); how many were set. Windows only (elsewhere 0).
int setThreadPriorities (const std::vector<std::string>& nameParts, bool messageThread, int priority);
// Round 190kb: the CPU cycles this process has run (Windows: QueryProcessCycleTime — counted at every context switch,
// exact, where the times above are sampled at the clock tick and miss or overcount short bursts) and the rate of
// that count (the time-stamp counter's, measured once); 0 where the platform has no such count. ThreadCpu::cycles:
// the same per thread.
std::uint64_t processCpuCycles();
double cpuCyclesPerSecond();
} // namespace lynx
