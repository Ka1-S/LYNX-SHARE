#pragma once

// The workspace desktop's layout model (docs/WORKFLOW_FL.md, P1 item 1): where each floating panel
// sits, which are visible, maximised or detached, their stacking order, snapping, and the text form
// saved in the user settings. No JUCE; the UI (ui/Workspace.*) draws and moves the panels.

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace lynx::workspace
{
enum class Panel : int { playlist, channelRack, pianoRoll, mixer, browser };
constexpr int panelCount = 5;

const char* panelId (Panel panel) noexcept; // "playlist", "channelRack"... (the saved-layout keys)
std::optional<Panel> panelFromId (const std::string& id);

struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;

    int right() const noexcept { return x + w; }
    int bottom() const noexcept { return y + h; }
    bool operator== (const Rect&) const = default;
};

struct PanelState
{
    Rect bounds;               // inside the desktop (also kept while hidden, maximised or detached)
    bool visible = false;
    bool maximised = false;    // fills the desktop
    bool detached = false;     // lives in its own OS window
    Rect windowBounds;         // detached: the window on screen (0 size = let the UI choose)
};

// Smallest panel size the UI allows when resizing.
constexpr int minWidth = 220, minHeight = 120;
// Default layout: room left for the Playlist's pattern picker and track names, and the smallest heights the
// Mixer and a Piano Roll tiled above it are given.
constexpr int playlistHeaderWidth = 260, mixerMinHeight = 400, pianoRollMinHeight = 300;
// Default Channel Rack width: at most its usual width (a bar of steps), at least this much beside the track names.
constexpr int rackMaxWidth = 720, rackMinWidth = 420;

// The panels that stay behind the others whatever is clicked: only the Browser, docked at the side as in FL. The
// Playlist is a window like the others: clicking it brings it to front over the Channel Rack, and F6 brings the rack
// back (FL 2026, round 168s / issue #32 [SHOT] r168s f02 → f03 → f04; it used to stay behind everything).
constexpr bool isBackground (Panel p) noexcept { return p == Panel::browser; }

// Issue #192: where the Browser is docked. FL 2026 docks its Browser in the main window's left or right dock site
// (TFruityLoopsMainForm SBLeftDockSite / SBRightDockSite, the Browser form is dockable): a docked Browser is not part of
// the desktop the other windows use, so a maximized Playlist, Piano roll, Channel rack or Mixer fills only what it
// leaves, and the whole desktop once the Browser is closed (Alt+F8) or detached. `none`: a Browser placed freely inside
// the desktop (layouts saved before round 190jz where it was moved off its side); it docks again at a side.
enum class Dock : int { left, right, none };
const char* dockId (Dock dock) noexcept; // "left", "right", "none" (the saved-layout values)

struct Layout
{
    std::array<PanelState, panelCount> panels {};
    Dock browserDock = Dock::left;
    std::vector<Panel> order; // stacking order, back to front (always all panels)
    Panel focused = Panel::playlist;
    int width = 0, height = 0; // desktop size the rects refer to

    PanelState& operator[] (Panel p) noexcept { return panels[(std::size_t) p]; }
    const PanelState& operator[] (Panel p) const noexcept { return panels[(std::size_t) p]; }

    // LYNX's default arrangement for a desktop of this size (FL's fresh-install desktop): Browser on the left,
    // the Playlist filling the rest (at the back of the stacking order), the Channel Rack floating over the Playlist; Piano Roll
    // and Mixer hidden with sensible rects for when they are first shown (Mixer at the bottom, Piano Roll above).
    static Layout makeDefault (int width, int height);
    // The desktop changed size while the layout is still the untouched default: take the default rects for
    // the new size (instead of scaling), keeping what is shown, maximised, the order and the focus.
    void relayoutDefault (int newWidth, int newHeight);

    void bringToFront (Panel p);
    bool isFrontmost (Panel p) const; // topmost visible, attached panel
    // The attached panels as they are drawn, back to front: the background panels (isBackground) first, then the
    // others in `order`.
    std::vector<Panel> stacking() const;

    // F-key behaviour: a hidden panel is shown and focused; a visible panel that is not focused is
    // brought to front and focused; the focused panel is hidden (focus moves to the next visible one).
    // Returns true if the panel is visible afterwards.
    bool toggle (Panel p);
    void show (Panel p);
    void hide (Panel p);
    void toggleMaximised (Panel p);

    // --- issue #192: the docked Browser ---------------------------------------------------------------------------
    // The Browser takes its side now: docked, shown and not in its own window.
    bool browserDocked() const noexcept;
    // The desktop the other panels use: all of it but a docked Browser's column.
    Rect freeArea() const noexcept;
    // Where a maximized panel goes: the free area (the Browser itself: the whole desktop).
    Rect maximisedArea (Panel p) const noexcept;
    // Docks the Browser at `side` (none: left where it is), full height at that edge, keeping its width; the floating
    // panels move out of its column.
    void dockBrowser (Dock side);
    // Puts a docked Browser back at its edge, full height, its width kept between minWidth and what leaves the other
    // panels minWidth (after a resize or a parse).
    void placeDockedBrowser();
    // The floating (attached, not maximized) panels other than the Browser kept inside the free area: those that reach
    // into the docked Browser's column move out of it, narrower only when they don't fit. Returns how many moved.
    int keepOutOfDockedBrowser();
    // The free area changed from `before` (the Browser shown, closed, docked elsewhere, detached or made wider): the other
    // panels move with it, as FL's windows do with its desktop (FL 2026, round 190jz shots fl-192-restored-windows-*: Alt+F8
    // moved the Mixer left by the Browser's width, and the Playlist touching the right edge kept that edge, getting wider).
    void followFreeArea (Rect before);

    // The desktop changed size: rects scale with it (proportionally), then are clamped back in.
    void resize (int newWidth, int newHeight);
    // The Browser at the left edge and the Playlist starting where it ends (kept so by resize()).
    bool browserDocksPlaylist() const;
    // The Playlist starts where the Browser (at the left edge) ends and fills the rest of the width.
    void redockPlaylist();
    // Brings panels that ended up (mostly) outside the desktop back inside. Returns how many moved.
    int bringBackOffscreen();

    // Text form (one line per panel, positions as fractions of the desktop so a layout saved on one
    // window size loads on another). parse() returns nothing for unreadable text.
    std::string serialise() const;
    static std::optional<Layout> parse (const std::string& text, int width, int height);
};

// Snapping a moved panel: each edge of `moving` that is within `threshold` px of an edge of another
// rect or of the desktop is pulled onto it (left/right to vertical edges, top/bottom to horizontal).
// `resizing`: only the edges in `edges` move (bitmask of the values below) and the size changes;
// otherwise the whole rect moves.
enum Edge : int { left = 1, top = 2, right = 4, bottom = 8 };
Rect snap (Rect moving, const std::vector<Rect>& others, int desktopWidth, int desktopHeight, int threshold = 8, int edges = 0);
} // namespace lynx::workspace
