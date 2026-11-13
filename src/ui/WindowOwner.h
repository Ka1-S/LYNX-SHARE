#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// Secondary windows (plugin, channel settings, EQ, automation) always stay above the LYNX main window,
// without staying above other programs: on Windows, the main window becomes their "owner".
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // call after the window is visible; sets the LYNX icon
// (it stays owned when JUCE recreates the native window later, e.g. setResizable after it is shown)
// The main window's native handle (HWND / X11 Window id; nullptr without one): a plugin's window in a separate
// process is kept above it (round 162d).
void* mainWindowHandle();

// Keys a secondary window does not use (no component in it took them) go to the main window's global shortcuts, as in
// FL (round 165q / issue #12: Space, F12 or a user's key did nothing while the Undo history, Settings, a plugin window...
// had the focus). keepAboveMainWindow installs the forwarding on the window; the main window sets the handler. A window
// that handles every key itself (a detached panel) sets the property "lynxOwnKeys".
void setSecondaryWindowKeyHandler (std::function<bool (const juce::KeyPress&)> handler);

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
} // namespace lynx
