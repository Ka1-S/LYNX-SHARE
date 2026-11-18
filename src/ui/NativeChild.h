#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <vector>

namespace lynx::nativechild
{
// Issue #116 (FL's docked plugin windows; design: docs/analise/REA_rodada181c.md): a third-party editor is a native child
// window, and a native child is always drawn above the lightweight JUCE components of the same window. So a docked
// plugin window and the panels that must be able to come in front of it are native child windows of the main window
// too, siblings whose stacking order the system keeps (as FL's MDI children). These helpers host a component that way
// inside `area` (the Workspace), in `area`'s coordinates.

// FL's docked plugin windows are the default (slice 3: every floating panel is a native child, overlays and DPI changes
// follow them). LYNX_DOCKED_PLUGINS=0 in the environment turns docking off: every plugin window is then FL's "Detached"
// and the panels are plain components (LYNX's behaviour before issue #116). On Linux and macOS docking is off unless
// LYNX_DOCKED_PLUGINS=1 (issue #165: X11 child windows lose text focus and leave stale pictures there; the docking glue
// is Win32-only; core/DockingPolicy.h).
bool dockingEnabled();

// Makes `c` a native child window of `area`'s top-level window (it must not have a parent component). False when that
// window has no native peer yet; `c` is left as it was. `styleFlags`: the ComponentPeer style flags of the child window;
// -1 = the ones it was attached with before (0 the first time). A juce::TopLevelWindow must get exactly its
// getDesktopWindowStyleFlags(): JUCE makes it a top-level window again whenever it sees other flags (round 190ev).
bool attach (juce::Component& c, juce::Component& area, int styleFlags = -1);
// Back to a plain component (no native window); the caller adds it to a parent again.
void detach (juce::Component& c);
bool isAttached (const juce::Component& c);

// Where an attached component is, in `area`'s coordinates, and moving it there. For a component that is not attached
// these are its plain bounds in its parent.
void place (juce::Component& c, const juce::Component& area, juce::Rectangle<int> boundsInArea);
juce::Rectangle<int> boundsIn (const juce::Component& c, const juce::Component& area);
// Is the attached component's native window exactly at `boundsInArea`, read from the system (not from JUCE's idea of
// its bounds)? True for a component that has no native window of its own. (Round 190cj: the dancers' windows are
// checked twice a second and put back when a zoom or display change, or anything else, left them elsewhere.)
bool isPlacedAt (const juce::Component& c, const juce::Component& area, juce::Rectangle<int> boundsInArea);
// After the main window's native window was made again, or `area` moved inside it: attached again if needed, and put
// back at the same place in `area`.
void refresh (juce::Component& c, juce::Component& area);
// Only the first half of refresh(): attached again when the main window's native window was made again (true then; the
// caller places it). Nothing is moved otherwise. (Round 190bj: refresh() places the window from JUCE's own idea of its
// bounds, which on Windows is relative to the parent's WINDOW rectangle while the placement is relative to its client
// area: every call moved a dancer by the frame's width, and each move dismissed an open menu, issue B1.)
bool reattachIfNeeded (juce::Component& c, juce::Component& area);

// Stacking among the attached siblings without disturbing the others: if `c` is not above `below` it goes just above it
// (under whatever was right above `below`); `below` == nullptr: nothing. Elsewhere than Windows: `c` comes to the front
// when it has to move.
void keepAbove (juce::Component& c, juce::Component* below);

// The main window moved to a display with another scale (issue #116): the attached children of `area`'s top-level
// window are placed again in its new physical pixels, and what hosts a plugin editor in them is told the new scale
// (JUCE tells a child window's listeners nothing by itself: only top-level windows get WM_DPICHANGED).
void followParentScale (juce::Component& area);

// An overlay of the main window (the setup wizard) in front of the native panels and docked plugin windows: an opaque
// native child window of `over`'s top-level window covering `over`, that shows a picture of what is under it (the
// lightweight components and the native children, taken when it opens and when it is resized) with `content` on top.
// Nullptr when `over` has no native window yet (the caller keeps `content` as a plain overlay then). It stays in front
// of the native children (raiseOverlays).
std::unique_ptr<juce::Component> makeOverlayHost (std::unique_ptr<juce::Component>& content, juce::Component& over);
void raiseOverlays();

// Floaters (round 188j, issue #116 slice 4: the Fruity Dance dancers): small attached children with per-pixel
// transparency (a component that is not opaque becomes a layered child window on Windows, a transparent subview on
// macOS) that stay in front of the native panels and docked plugin windows, under the overlays. Clicks on their
// transparent pixels reach the window under them. raiseOverlays raises the floaters first, then the overlays.
// On Windows that holds for every point the component's hitTest refuses, opaque pixels too (round 190cj: the dancer
// with FL's "Solid" off, its arms and its reflection): the system asks the window, which answers "transparent" there.
// Elsewhere (Linux: X11 child windows are drawn without blending) supportsFloaters() is false and the caller keeps
// drawing in the main window's components.
bool supportsFloaters();
void addFloater (juce::Component& c);
// An attached floater that never takes the mouse or a drop (round 190fc: the first-song tour's dimmed layer): the system
// passes every click, wheel and file drop through it to the window under it (Windows: WS_EX_TRANSPARENT on its layered
// window; elsewhere nothing more than the windowIgnoresMouseClicks flag it was attached with).
void passInputThrough (juce::Component& c);
void removeFloater (juce::Component& c);
// A floater or an overlay with an attached sibling above it that is neither: raiseOverlays (Windows; a cheap walk of
// the siblings above each one). Elsewhere: nothing (raiseOverlays already runs on every stacking change).
void keepFloatersInFront();
// Round 190kr (QA-11): `c`'s native window is a child window (a docked panel: the Playlist, Piano roll, Mixer...) that
// lies wholly under visible, opaque sibling windows above it, or under its parent's edges (and the same for each parent
// child window): nothing it paints or composes reaches the screen. The Playlist behind Thief's maximised Channel rack.
// Round 190la: also when the top-level window it is in (the main window, a detached panel, a plugin window) lies wholly
// under one visible, opaque top-level window of any program (a maximised browser over LYNX): layered, click-through,
// cloaked, minimised and region windows never count (LYNX_OCCLUSION=0: this part off, A/B).
// A component without a native window of its own and every platform but Windows: false.
// Cheap (a walk of the windows above it: a few user32 calls; the top-level answer is kept 8 ms). LYNX_COVERCHECK=0:
// always false (A/B).
bool coveredBySiblingWindows (const juce::Component& c);
// `c` shows on screen: isShowing() (or an off-screen picture route, ui/ShotShowing.h) and not covered as above. The live
// views' frame steps run only then (ui/FrameTicker.h).
bool showsOnScreen (const juce::Component& c);
// diagnostics (--uiperf): steps the frame tickers left out because their view was covered
std::atomic<int>& coveredSkips();
// diagnostics (LYNX_OVERLAY_STATS, LYNX_TICKER_LOG): how coveredBySiblingWindows decided for `c` — its window, what of
// it can show, and every visible sibling window above it (and above its parents) with its rectangle and flags
juce::String describeCover (const juce::Component& c);
// The attached children of a native window (the main window's handle), from the bottom of their stacking order up.
std::vector<juce::Component*> attachedChildren (void* parentWindow);

// Issue #161: a click in an attached component when its window is not the foreground window (LYNX was in another app's
// background, or a detached panel / plugin window of LYNX was the active one) activates the main window, as a click
// anywhere in FL's window does, so its shortcuts (Space...) work at once. JUCE answers the system's "activate?" question
// from the clicked window's own component, and the panels don't take the keyboard focus on a click (their keys reach the
// main window), so it said no. A component that takes the focus on a click activates as before; a click blocked by a
// modal dialog is left to JUCE; inside the active window nothing changes (the focus stays where it is: a text field, a
// plugin's editor). After the activation the main window gets the system focus, and JUCE gives it back to the control
// that had it (else MainComponent takes it).
bool clickActivatesWindow (const juce::Component& c, bool windowIsForeground);
// Tests (Windows; elsewhere 0): what the attached component's native window answers the system's WM_MOUSEACTIVATE
// (MA_ACTIVATE = 1, MA_NOACTIVATE = 3), as if LYNX were in the background when `asInactive`.
int mouseActivateAnswerForTest (const juce::Component& c, bool asInactive);

// --selftest (issue #161): the decision above for a panel-like component (no focus on a click), a modal dialog's block and,
// on Windows, the answer of a real attached child window of a never-shown window. "OK" or "ERROR ...".
juce::String clickActivationSelfTest();

// Round 190ev (issue #152): a click anywhere in the attached component `c` brings it in front of its siblings (under the
// floaters and overlays), also a click that lands in a native window inside it that JUCE never hears of (a plugin's own
// view in a docked plugin window): FL's docked windows come in front on any click. `onClick` (optional) runs after a
// click of that kind. Windows: a message hook of the UI thread sees the button-down messages; elsewhere nothing (the
// plugin views there are not native children of LYNX's windows in the same way).
void raiseOnAnyClick (juce::Component& c, std::function<void()> onClick = {});

// Keys nothing in an attached component used: they don't travel up to the main window by themselves (it is another
// native window), so `c` hands them to the main window's shortcuts (the secondary windows' route, ui/WindowOwner.h).
void forwardUnusedKeys (juce::Component& c);

// Tests (Windows; elsewhere false / nullptr): `a` is above its sibling `b` in the native stacking order; a native window's
// parent window (a child window) and owner (a top-level window kept above another).
bool isAbove (void* a, void* b);
// `c` (attached) is at its bounds in its parent's physical pixels (within a pixel); the main window's scale listeners
// called as on a display change (so the desktop's native children follow it).
bool placedPhysically (const juce::Component& c);
void notifyScaleListenersForTest (juce::Component& top);
void* parentOf (void* window);
// Tests (Windows; elsewhere empty): an attached component's native window in its parent's client area, that client
// area (at 0,0), and any native window's rectangle on the screen; all in physical pixels, read from the system.
juce::Rectangle<int> nativeRectInParent (const juce::Component& c);
juce::Rectangle<int> nativeParentClientSize (const juce::Component& c);
juce::Rectangle<int> nativeScreenRect (void* window);
void* ownerOf (void* window);
// Tests (Windows; elsewhere false / nullptr): the native window the mouse would reach at `c`'s local point (a layered
// window's transparent pixels let it through), and whether a native window is layered (per-pixel transparency).
void* windowAt (const juce::Component& c, juce::Point<int> localPoint);
bool isLayered (void* window);
// Tests (Windows; elsewhere nothing / nullptr), round 190ev: the innermost first child window of a native window (a
// plugin's own view inside a docked frame), and a left click posted to a native window of LYNX's own process at a point
// of its client area, in physical pixels (a message to that window only: nothing goes through the system's input).
void* innermostChild (void* window);
void postClickForTest (void* window, juce::Point<int> clientPoint);
// Tests (Windows; elsewhere false), round 190fc: Space pressed as the system delivers it — key-down and key-up posted to the
// window that has the system's keyboard focus (when it is one of LYNX's own; else `fallback`), then the thread's
// messages pumped for a moment (TranslateMessage makes the character, as for a real key). True when it was posted.
bool pressSpaceForTest (void* fallback);
// Tests (Windows; elsewhere 0): the system's hit test of the component's own native window at its local point: 1
// (HTCLIENT) = the window takes the mouse there, -1 (HTTRANSPARENT) = the click goes to the window under it. (Round
// 190cj: an attached component that is not opaque answers "transparent" wherever its hitTest says no.)
int hitCodeAt (const juce::Component& c, juce::Point<int> localPoint);

// Round 190jy (queue item 44, core/DockStack.h editorsAbove): docked LYNX editors drawn by the main window itself show
// through holes in the native siblings below them.
// `holes`: rectangles in the attached component's own native window, physical pixels (0,0 = its top-left); empty = the
// whole window shows (no window region). The window is only touched when its holes change. Windows only (elsewhere
// nothing: the editors stay native child windows there).
void setHoles (juce::Component& c, const std::vector<juce::Rectangle<int>>& holes);
std::vector<juce::Rectangle<int>> holesOf (const juce::Component& c);
// Whether any part of `rectInWindow` (physical pixels of that native window) is in one of its holes (the window has a
// region and the rectangle is not wholly inside it). Windows only; elsewhere false.
bool cutsInto (void* window, juce::Rectangle<int> rectInWindow);
// Runs (message thread) after an attached child window moved, was resized, shown, hidden or put elsewhere in the
// stacking order, or after the floaters / overlays were raised (nullptr: nothing). The workspace keeps the holes current.
void setStackingListener (std::function<void()> listener);
// The main window's client-area rectangle of `boundsInArea` (in `area`'s coordinates), in physical pixels, rounded edge
// by edge (as its own drawing of a component there is clipped).
juce::Rectangle<int> physicalInTop (const juce::Component& area, juce::Rectangle<int> boundsInArea);
// An attached component's native window rectangle in its parent's client area, physical pixels (empty: not attached).
juce::Rectangle<int> rectInParentWindow (const juce::Component& c);
// The click raise (raiseOnAnyClick) and the key forwarding (forwardUnusedKeys) of a component that stops being an
// attached window (it is drawn by the main window now).
void stopRaiseOnAnyClick (juce::Component& c);
// Puts the attached `c` right above `below` in the native stacking order (nullptr: at the bottom); Windows only.
void placeJustAbove (juce::Component& c, juce::Component* below);
// --selftest (round 190jy): two attached children of a never-shown window: a hole set and read back (the system's region,
// its hit test there), cleared again, and placeJustAbove's order. "OK", "OK (not available ...)" elsewhere, or "ERROR ...".
juce::String editorHolesSelfTest();
void stopForwardingUnusedKeys (juce::Component& c);
} // namespace lynx::nativechild
