#pragma once

#include <array>

// Shared pieces of the Piano Roll and Playlist editors: the edit tools, the global grid (with the
// per-editor "Follow global" choice and the default note length) and the Note inspector.

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/EditorGestures.h"
#include "core/FollowPause.h"
#include "core/ToolbarLayout.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/ValueControl.h"

namespace lynx
{
// --- shared title bar ---------------------------------------------------------------------
// The Playlist and Piano Roll carry their toolbar in their window's title bar, as FL Studio's editors do
// (docs/WORKFLOW_FL.md, "Layout"): the workspace frame lays the editor over its title row and keeps
// `left` px for its menu button and title and `right` px for its window buttons; the editor lays out
// its toolbar between them.
class SharesTitleBar
{
public:
    virtual ~SharesTitleBar() = default;
    virtual void setTitleBarInsets (int left, int right) = 0;
};

// --- edit tools ------------------------------------------------------------------------
// Rules shared by every tool: right-click deletes, Alt bypasses the grid, Ctrl selects
// (Ctrl+drag draws a selection box). Right-press-and-hold on empty grid gives a temporary Knife.

struct EditToolInfo
{
    EditTool tool;
    Icon icon;
    const char* name;        // English, translated with tr()
    const char* description; // English, translated with tr()
    char key;                // single-key shortcut (only while the typing piano is off)
};

const std::vector<EditToolInfo>& editTools();
// The tool's key map name ("pencil", "drumPaint"...): the editors' actions are "pianoRoll.tool.<it>" / "playlist.tool.<it>".
const char* editToolId (EditTool tool);

// The mouse cursor of each editor gesture (core/EditorGestures.h): the system arrows for moving,
// resizing and selecting, LYNX's own tool icons (outlined, so they read on any background) for
// drawing, painting, erasing, muting, slicing, zooming, listening and slipping. Built once.
juce::MouseCursor editorCursor (gesture::Cursor cursor);

// Hint bar text for what a left press would do (`playlist` words it for clips instead of notes),
// followed by the gestures that apply on top of it. The editors' HintSource uses it.
juce::String actionHint (gesture::Action action, bool playlist);
const EditToolInfo& editToolInfo (EditTool tool);

class ToolStrip;

// FL's Zoom tool icon right-click menu (round 190mp, DE-74; capture 190mp A3.1, fl-pr-zoom-menu.png / fl-pl-zoom-menu.png):
// Zoom in, Zoom out | Quick zoom 1–3 | Zoom out far, Zoom on selection | Step sequencer mode zoom (Piano roll, which LYNX
// has not: greyed) or Zoom on performance clips (Playlist), each running the editor's key map action (`prefix` =
// "pianoRoll." / "playlist."), with its key from `keyText` ("\tShift+1"...).
juce::PopupMenu quickZoomMenu (const std::string& prefix, const std::function<bool (const std::string&)>& run,
                               const std::function<juce::String (const std::string&)>& keyText, bool performanceClips);
// FL's middle + right click (round 190mp, DE-62; capture 190mp A3.2, fl-pr-midright-menu.png): the editor's main menu at
// the mouse with a "Mode" column of the tools on its left (the one in use ticked).
juce::PopupMenu toolModeMenu (const ToolStrip& strip, const std::function<void (EditTool)>& choose, const juce::PopupMenu& mainMenu);

// A row of radio icon buttons, one per tool. Icon size and button width follow the toolbar icon size
// (Options > Toolbar), shared by every strip; the owner re-lays the strip out after a change.
class ToolStrip final : public juce::Component
{
public:
    explicit ToolStrip (bool includeSlip);
    static inline toolbarlayout::IconSize iconSize = toolbarlayout::IconSize::medium;

    EditTool getTool() const noexcept { return tool; }
    void setTool (EditTool newTool);
    int getIdealWidth (int height) const;

    std::function<void (EditTool)> onChange;
    // The tooltips with the keys the key map gives the tools now (`keyOf` returns "" for a tool without one).
    void refreshKeys (const std::function<juce::String (EditTool)>& keyOf);
    // Round 190ig (FM-26): the Pattern layout's Playlist in FL's order and glyphs — delete ⊘ for the eraser, slip ◂▸ before
    // the knife (FL: delete, mute, slip, slice); off: LYNX's own (the other layouts, the Piano roll)
    void setFlPlaylistLook (bool on);

    void resized() override;
    void paint (juce::Graphics& g) override; // round 170e3: the tools sit in one well, apart from the header's other controls
    // the wheel over the strip steps through the tools
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    // One tool to the right (+1) or left (−1) in the strip's order, stopping at the ends (FL's wheel order; round 190mp:
    // also the editors' right-hold + wheel, DE-61, capture 190mp A3.3). Fires onChange.
    void step (int direction);
    // The strip's tools in their order (the editors' "Mode" column, DE-62).
    const std::vector<EditTool>& toolList() const noexcept { return tools; }
    // FL's Zoom tool icon: a right-click opens its quick-zoom menu (round 190mp, DE-74); the owner builds and shows it.
    std::function<void (juce::Component& zoomButton)> onZoomMenu;

private:
    EditTool tool = EditTool::pencil;
    std::vector<std::unique_ptr<IconButton>> buttons;
    std::vector<EditTool> tools;
    std::vector<juce::Rectangle<int>> wells; // one per tool group (round 170e3)
    bool flPlaylistLook = false;
};

// --- grid --------------------------------------------------------------------------------
namespace grid
{
enum class Kind
{
    none,    // free (whole ticks)
    screen,  // "Line": follows the lines the editor is drawing at its zoom (FL's "Line")
    nearest, // "Events": the nearest note / clip edge (and ghost notes); otherwise the lines
    ticks,   // a fixed fraction of a step or beat
    bar      // whole bars
};

struct Option
{
    Kind kind;
    int ticks; // for Kind::ticks
    const char* name;
};

const std::vector<Option>& options();
constexpr int noneOption = 0;
constexpr int screenOption = 1;

// Editor combo: id 1 = "Follow global", id i + 2 = options()[i]. Global combo: id i + 1.
constexpr int followId = 1;
juce::String mainText(); // "Main: Line": the editors' entry that follows the toolbar snap, by its current name
void fillEditorBox (juce::ComboBox& box);
void fillGlobalBox (juce::ComboBox& box);

// The option an editor combo stands for (resolving "Follow global").
const Option& resolve (const juce::ComboBox& editorBox);

// What an option does, in one sentence (the hint bar shows it for the chosen option).
juce::String describe (const Option& option);
} // namespace grid

// A snap selector that explains its current choice in the hint bar: the component's hint text (ui::setHint
// or the tooltip) followed by what the chosen option does. `editor`: the box has the "Main" (follow the
// toolbar) entry. The wheel steps through the options.
// A drop-down with a small icon before its text (round 170e, Busy Works review #5: the editors' snap, chord and scale
// boxes were text only). `iconLit` (optional): the icon is drawn in the accent colour while it returns true.
class IconComboBox : public juce::ComboBox
{
public:
    explicit IconComboBox (Icon icon = Icon::none) : icon (icon)
    {
        // issue #218 (round 190ng): the editors' and toolbars' drop-downs are mouse-only, the PC keyboard stays with the
        // view (Enter opened a clicked box again; ui/KeyFocus.h)
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }
    void setIcon (Icon newIcon);
    Icon getIcon() const noexcept { return icon; }
    std::function<bool()> iconLit;

    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::Rectangle<float> iconArea() const;

private:
    Icon icon;
};

class SnapBox : public IconComboBox,
                public ui::HintSource
{
public:
    explicit SnapBox (bool editor);
    ~SnapBox() override;
    ui::Hint hintAt (juce::Point<int>) override;
    // the "Main" entry names the toolbar snap it follows ("Main: Line"); called when that snap changes
    void refreshMainText();

private:
    struct Watch final : juce::ChangeListener
    {
        SnapBox* owner = nullptr;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { owner->refreshMainText(); }
    };
    bool editor;
    Watch watch;
};

// State the editors share with the main window's toolbar. Listeners hear about every change.
class EditorGlobals final : public juce::ChangeBroadcaster
{
public:
    static EditorGlobals& get();

    int getGridOption() const noexcept { return gridOption; }
    void setGridOption (int option);
    void toggleGrid(); // Backspace: between the last grid and "No snap"

    // Length of new notes in ticks; 0 = the length of the last note clicked or resized.
    std::int64_t getDefaultNoteLength() const noexcept { return defaultNoteLength; }
    void setDefaultNoteLength (std::int64_t ticks);

    // While the typing piano owns the letter keys, the editors' single-key shortcuts (tools, channel
    // switching) are off. The main window's typing-piano toggle (P1 #14) sets this.
    bool typingPiano = true;

    // "Remember seek time" (the Stop button's right-click menu, as in FL's toolbar): Stop goes back to the
    // position last clicked on the Playlist or Piano roll ruler (`seekMark`, in ticks; -1 = none) or else to where
    // playback started, instead of the start. On by default, as in FL (issue #169, REA round 190gy: FL's setting
    // "RemSeekTime" starts as 1). `seekMarkSongMode`: the mark is a Song position (else a pattern position);
    // `seekMarkSerial` counts the clicks, so the transport sees a click made while it plays.
    bool rememberSeek = true;
    std::int64_t seekMark = -1;
    bool seekMarkSongMode = true;
    std::uint32_t seekMarkSerial = 0;
    void markSeek (std::int64_t tick, bool songMode) noexcept
    {
        seekMark = tick;
        seekMarkSongMode = songMode;
        ++seekMarkSerial;
    }

    // The Playlist's STRETCH switch (FL's ClipStretchBtn "Stretch audio when resizing audio clips", Shift+M) and Edit ›
    // "Create variants when stretching audio clips" (FL's global 0x15452e5, on by default; REA round 168f). The second
    // is saved with the UI options.
    bool stretchOnResize = false;
    bool createVariants = true;
    // FL's Playlist menu › Edit options (round 189g; FLEngine ShortcutsModule actions, saved with the UI options):
    // "Allow resizing from left" (EELeftResizingAction Ctrl+Alt+Home, the PLForm's +0xb4a; off = a clip's left edge moves
    // it, FUN_00e8e840), "Nudge clips with mouse wheel" (EEShiftMouseWheelNudgeAction, Checked in FL's form: Shift+wheel
    // moves clips by the snap, FUN_00e82270), "Create new clips with manual fades" (EECreateNewClipsWithManualFades, global
    // 0x152c364) and "Snap fade handles" (EESnapFadeHandles, 0x152c365). FL's defaults.
    bool leftResizing = true;
    bool wheelNudging = true;
    bool manualFades = false;
    bool snapFadeHandles = false;
    // round 190cg: "Automation follows clips" (Live's lock-envelopes switch turned round): a clip moved or copied along
    // its track takes the track's lane automation with it. -1 = as the layout: on in the Clip-launch workflow, off in
    // the others (which is what LYNX did before); 0 / 1 = set by hand.
    int automationFollowsClips = -1;
    // round 190db: the window's workflow is the Clip-launch workflow (set with the layout style). The audio editor draws
    // a warped clip in beats there, as Live's Clip View does; the Pattern workflow keeps its file-time view.
    bool clipLaunchWorkflow = false;
    // FL's Playlist menu › Select › "Magic lasso" (EESelMagicAction, the PLForm's +0xb49; off in FL): circling clips with
    // the mouse selects them (FUN_00e6ff80).
    bool magicLasso = false;
    // FL's Piano roll menu › Select › "Magic lasso" (round 190mp, DE-71; capture 190mp A3.4: the Select submenu's last item,
    // after a line, off by default): the same gesture over the notes. The Piano roll's own switch (saved).
    bool pianoRollMagicLasso = false;

    // "Follow" (FL's auto scrolling, round 162 / issue #9): the Playlist (Song mode) and the Piano Roll page along
    // with the playhead while playing; off, they stay where the user scrolled. Saved with the UI options.
    bool isFollowing (bool pianoRoll) const noexcept { return pianoRoll ? followPianoRoll : followPlaylist; }
    void setFollowing (bool pianoRoll, bool on, bool save = true); // save: call onOptionsChanged
    // The Channel Rack's and the Audio Editor's Follow (issue #12): the rack pages to the playing step of a long
    // pattern, the Audio Editor to its playback position.
    enum class FollowView { playlist, pianoRoll, channelRack, audioEditor };
    static_assert ((int) FollowView::audioEditor == followpause::audioEditor && (int) FollowView::playlist == followpause::playlist);
    bool isFollowingView (FollowView v) const noexcept
    {
        return v == FollowView::channelRack ? followRack : v == FollowView::audioEditor ? followAudioEditor : isFollowing (v == FollowView::pianoRoll);
    }
    void setFollowingView (FollowView v, bool on, bool save = true);
    // Round 190im (R15, owner decision L3; Live's manual 6.2 / 8.7: "Follow will pause if you make an edit or scroll the
    // view horizontally", it goes on with the next play / stop or a Follow click): a view's Follow pauses after a scroll
    // or an edit by hand. −1 = as the layout: on in the Clip-launch workflow, off in the others until FL's behaviour is
    // captured; 0 / 1 = set by hand (Options › "Auto scrolling pauses on a scroll or an edit", saved with the UI options).
    int followPausesOnEdit = -1;
    bool followPausing() const noexcept { return followPausesOnEdit < 0 ? clipLaunchWorkflow : followPausesOnEdit != 0; }
    void pauseFollow (FollowView v) noexcept { followState.pause ((int) v, followPausing(), isFollowingView (v)); }
    // round 190kp: a sideways scroll a view saw between two frames (not counted right after a layout switch)
    void pauseFollowForScroll (FollowView v) noexcept
    {
        followState.pauseForScroll ((int) v, followPausing(), isFollowingView (v), juce::Time::getMillisecondCounterHiRes());
    }
    // (round 190kp: a pause counts only where pausing is on, see core/FollowPause.h)
    bool isFollowPaused (FollowView v) const noexcept { return followState.isPaused ((int) v, followPausing()); }
    // Follow is on for the view and not paused now
    bool followsNow (FollowView v) const noexcept { return followState.follows ((int) v, isFollowingView (v), followPausing()); }
    // the song changed during a gesture or a key of the view (an edit by hand)
    void pauseFollowIfEdited (FollowView v, std::uint64_t before, std::uint64_t after) noexcept
    {
        if (before != after)
            pauseFollow (v);
    }
    void resumeFollow (FollowView v) noexcept { followState.resume ((int) v); }
    // each view's frame step tells the transport's state: a play or a stop lifts every pause
    void noteTransport (bool playing) noexcept { followState.noteTransport (playing); }
    // round 190kp: the layout style changed (not a scroll by hand): every pause is lifted
    void followLayoutChanged() noexcept { followState.layoutChanged (juce::Time::getMillisecondCounterHiRes()); }
    // Called after an option the main window saves changed here (it writes ui-options.txt).
    std::function<void()> onOptionsChanged;

private:
    EditorGlobals() = default;
    int gridOption = grid::screenOption, lastGridOption = grid::screenOption;
    std::int64_t defaultNoteLength = 0;
    bool followPlaylist = true, followPianoRoll = true, followRack = true, followAudioEditor = true;
    followpause::State followState; // round 190im: the pauses [FollowView] (round 190kp: core/FollowPause.h)
};

// The global grid selector of the main toolbar: left-click picks the grid, right-click sets the
// default note length.
class GlobalGridBox final : public SnapBox,
                            private juce::ChangeListener
{
public:
    GlobalGridBox();
    ~GlobalGridBox() override;

    void mouseDown (const juce::MouseEvent& e) override;
    static void showDefaultLengthMenu (juce::Component* target);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

// --- overview strip ------------------------------------------------------------------------
// The strip above the Piano Roll's and the Playlist's ruler: the whole content in miniature (drawn by
// `paintContent`) with the visible window outlined. Drag the window to scroll, drag one of its edges to
// zoom, click elsewhere to jump there (and keep dragging), double-click to show everything.
// Units are the editor's (ticks); the editor calls setRange whenever its view changes.
class OverviewBar final : public juce::Component
{
public:
    OverviewBar();

    void setRange (double total, double start, double length);
    double getTotal() const noexcept { return total; }
    // round 190im (R5, DC-6): the song position as a line in the strip while playing (< 0: none, as FL shows none
    // stopped); only the old and new few pixels are repainted, and only when the line's pixel changes
    void setPosition (double units);

    // draws the miniature into `area` (the whole strip), `total` units across
    std::function<void (juce::Graphics&, juce::Rectangle<float> area, double total)> paintContent;
    // the user moved (same length) or resized (zoom) the window
    std::function<void (double start, double length)> onChange;
    std::function<void()> onShowAll;
    double minLength = 1.0; // the shortest window the editor can zoom to

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    juce::Rectangle<float> windowArea() const;
    gesture::OverviewGrab grabAt (int x) const;
    void updateCursor (gesture::OverviewGrab g);

    double total = 1.0, start = 0.0, length = 1.0;
    double position = -1.0;
    int positionX = -1; // (the pixel the line was last drawn at, -1: none)
    int positionXFor (double units) const;
    gesture::OverviewGrab grab = gesture::OverviewGrab::none, hoverGrab = gesture::OverviewGrab::none;
    gesture::OverviewWindow grabFrom;
    bool dragging = false;
};

// --- Note inspector ------------------------------------------------------------------------
// Edits velocity, pan, release, fine pitch, mod X/Y, slide and porta of `indices` (notes of the current
// pattern) as one undo step on Apply; Revert puts the controls back. Time and length are offered when a
// single note is inspected. `focus` is the note whose values are shown.
std::unique_ptr<juce::Component> makeNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus);

// Opens the inspector in a call-out pointing at `screenArea`.
void showNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus, juce::Rectangle<int> screenArea);
} // namespace lynx
