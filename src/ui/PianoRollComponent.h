#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/EventPaneSplit.h"
#include "core/LiveGrid.h"
#include "core/MagicLasso.h"
#include "core/MpeTab.h"
#include "core/NoteTools.h"
#include "core/PianoKeys.h"
#include "core/ScaleSnap.h"
#include "engine/AudioEngine.h"
#include "ui/TimerWhileShowing.h"
#include "ui/EditorTools.h"
#include "ui/FrameTicker.h"
#include "ui/MotionOverlay.h"
#include "ui/FlMods.h"
#include "ui/NoteToolDialogs.h"
#include "ui/ValueControl.h"
#include "core/NoteEdit.h"
#include "core/MidiImport.h"
#include "core/ScriptDialog.h"

namespace lynx
{
class ChordHelperPanel; // (round 190mc, ui/ChordHelper.h)

// Piano Roll for the selected channel in the current pattern.
//  - a tool strip (Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen; the wheel over it switches);
//    with the Pencil: left click on empty space creates a note (default length, or the last used one)
//    and drags it, dragging the body moves, either edge resizes (the left edge can be turned off)
//  - the cursor, the hover highlight and the hint bar show what a click would do (core/EditorGestures.h)
//  - every tool: right click deletes (dragging deletes several; press and hold on empty grid = Knife),
//    Ctrl+drag selects (Ctrl+click adds/removes), Alt ignores the grid; Shift+drag on a note drags a copy
//  - while moving: Shift pressed after the click keeps the pitch, Ctrl keeps the time
//  - double click on a note opens the Note inspector (applies to the selection)
//  - channel selector, previous/next channel keys and ghost notes of the other channels (Alt+V);
//    double right-click a ghost note to switch to its channel
//  - the bottom lane edits velocity, pan or fine pitch (combo in its corner): drag draws, right-drag a
//    line, Shift+drag one value, Alt+click resets; Alt+wheel over a note changes it (Ctrl+Alt: fine)
//  - wheel scrolls (smoothly), Shift+wheel horizontal, Ctrl+wheel zooms at the mouse, Ctrl+Alt+wheel
//    zooms vertically, Shift+Alt+wheel nudges the selection; middle-drag pans (on the keyboard: zooms)
//  - keyboard: click previews, Ctrl+click selects the key's notes; ruler: Ctrl+drag selects a time range
//  - keys (via handleKey): Delete, Backspace (global grid on/off), Ctrl+A/C/X/V/B, Ctrl+Q quantise,
//    Shift+Q quantise starts, Ctrl+U chop, Ctrl+G join, Ctrl+L legato, Shift+D shrink to grid,
//    Ctrl+Shift+Left/Right rotate, Ctrl+M mute, Ctrl+P porta, Esc, arrows (Shift = octave/bar)
namespace pianoroll
{
// the toolbar's chord stamp box when the full box (100 px) doesn't fit: its icon and its arrow
constexpr int compactChordBoxWidth = 46;
} // namespace pianoroll

class PianoRollComponent final : public juce::Component,
                                 public SharesTitleBar,
                                 public ui::HintSource,
                                 public juce::FileDragAndDropTarget,
                                 public juce::DragAndDropTarget,
                                 private juce::ChangeListener,
                                 private juce::Timer,
                                 private juce::ScrollBar::Listener
{
public:
    explicit PianoRollComponent (AudioEngine& engine);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void setTitleBarInsets (int left, int right) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void modifierKeysChanged (const juce::ModifierKeys& mods) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void showToolModeMenu(); // FL's middle + right click (round 190mp, DE-62)
    void feedMagicLasso (juce::Point<int> p, bool buttonDown); // FL's Magic lasso (round 190mp, DE-71)
    // trackpad pinch (macOS; Windows touch screens): time zoom around the pointer, with Alt the vertical zoom (round 170e)
    void mouseMagnify (const juce::MouseEvent& e, float scale) override;

    // Editing shortcuts; the main window forwards keys when the Piano Roll is visible.
    bool handleKey (const juce::KeyPress& key);
    // The key map's actions of this editor (its tools and single letters: "pianoRoll.tool.knife"...); false if not one.
    bool runKeyAction (const std::string& id);
    // The tool buttons' tooltips show the keys the key map gives them (`keyOf`: action id -> key text, "" = none).
    void refreshToolKeys (const std::function<juce::String (const std::string&)>& keyOf);

    // The key map's pianoRoll.* actions (tool dialogs, zoom presets). Returns false for unknown ids.
    bool runAction (const std::string& id);
    // round 190mr (DE-63): a left press at `at` held still, its ring as after `elapsedMs`, its action now, the release
    void pressForSnapshot (juce::Point<int> at, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier));
    void holdRingForSnapshot (int elapsedMs) { holdSince = juce::Time::getMillisecondCounter() - (juce::uint32) elapsedMs; repaint(); }
    void fireHoldForSnapshot() { holdReadsMouse = false; holdFired (holdSerial); holdReadsMouse = true; }
    void releaseForSnapshot (juce::Point<int> at);
    void dragForSnapshot (juce::Point<int> at, juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)); // (round 190mv)
    bool holdPendingForSnapshot() const noexcept { return hold != Hold::none; }
    std::int64_t gridTicksForSnapshot() const { return gridTicks(); }
    juce::PopupMenu moreMenuForSnapshot() { return moreMenu(); } // --userfixshot190ib (issue #183's item)
    // The shortcut text of a key map action for menus ("\tCtrl+Q"; empty when unbound), set by the main window.
    static inline std::function<juce::String (const std::string& id)> shortcutText;
    // The Piano roll's main menu (its window's ▾ items), for FL's middle + right click tool menu (round 190mp, DE-62).
    std::function<juce::PopupMenu()> mainMenu;

    // Note tool dialogs on the selection (or all of the channel's notes).
    void openNoteTool (NoteTool tool);

    // Scale snap: new and moved notes (and incoming MIDI) land on the highlighted scale.
    bool isScaleSnapOn() const noexcept { return scaleSnap; }
    void setScaleSnap (bool on);
    bool hasScale() const { return scaleIndex() > 0; } // a scale is chosen in the toolbar
    int scaleTonic() const { return hasScale() ? scaleRoot() : -1; } // its root (pitch class), or -1
    // "Fold to the scale" (round 166d, issue #14; LYNX's own — FL only highlights the scale): only the in-scale keys
    // are shown, and the notes on the other keys are hidden and left alone by the edits.
    bool isFoldedToScale() const noexcept { return foldToScale; }
    void setFoldToScale (bool on);
    // FL's ghost channels (round 166d): which channels' notes show as ghosts (ChannelDef::ghostHidden)
    juce::PopupMenu ghostChannelsMenu();

    // Centers the view on the channel's notes and, with `fitPattern`, zooms to show the whole pattern
    // (called on open).
    void scrollToNotes (bool fitPattern = true);
    void centreOnTick (double tick, double at = 0.5); // round 190mk (DE-33): this pattern tick at `at` of the view's width

    // FL's parameter menu "Edit events in piano roll" (REA round 146): the bottom lane shows and draws the current
    // pattern's events of `target` (the lane box lists the pattern's event lanes after the note properties).
    void showEventLane (const AutomationTarget& target);
    // The target the bottom lane edits when it shows pattern events.
    std::optional<AutomationTarget> eventLaneTarget() const;

    // For tests/snapshots: selects all of the channel's notes.
    void selectAll();

    // --- round 168g (FL's Piano roll menu, docs/analise/REA_rodada168g.md) ---------------------------------------------
    // View › Note grid highlights (Alt+B; FL's ini "BlackKeys", on by default): the rows of the black keys (or of the keys
    // out of the chosen scale) are shaded; off, every row looks the same. View › Show chord name for selection (off by
    // default): a selection of 2 to 10 different keys shows its chord name over the grid's top-left corner. The main
    // window saves both with the UI options.
    static inline bool gridHighlights = true, showChordName = false;
    // View › Scale highlighting › "Black notes" / "Default" (FL 2026, round 190mq, issue #212): with no scale chosen, the
    // rows of the black keys are shaded (FL's Black notes, LYNX's "No scale": on) or none is (FL's Default: off). A
    // chosen scale (or Automatic) shades by the scale instead, whatever this says. Saved with the UI options.
    static inline bool blackKeyRows = true;
    // View › Keyboard style (round 190j, discussion #136; LYNX's own): flat = FL's keyboard (the default), piano = a
    // traditional keyboard with a line between every two white keys (core/PianoKeys.h). Saved with the UI options.
    static inline pianokeys::Style keyStyle = pianokeys::Style::flat;
    void setKeyStyle (pianokeys::Style style);
    static inline bool songBarsRuler = false; // round 177d: the ruler numbers song bars (off = FL: pattern bars)
    // FL's General › "Monophonic painting in piano roll" (round 189o, off): on, the Paint tool paints monophonically
    // without Shift (noteedit::monophonicPaint). Saved with the UI options.
    static inline bool monophonicPainting = false;
    // Issue #183: "Preview notes while playing" (the Piano roll's menu; LYNX's, off: FL keeps the notes you place silent
    // while the song or pattern plays). Saved with the UI settings (prPreviewPlaying).
    static inline bool previewWhilePlaying = false;
    // the keys of the selection, on every change (the toolbar's chord panel)
    static inline std::function<void (const std::vector<int>&)> onSelectedKeys;
    // View › Background waveform (Alt+N): an audio file dropped on the Piano roll is drawn behind the notes from the
    // pattern's start at the song tempo (FL's FUN_00e70bf0 / FUN_00e45a00); one for every editor, not saved.
    static bool isBackgroundWaveShown() noexcept;
    void setBackgroundWaveShown (bool on);
    void loadBackgroundWave (const juce::File& file); // (and shows it)
    // FL's drop on the editor (FUN_00e70bf0): a score (.fsc) opens (replacing the channel's notes), a MIDI file goes
    // through the MIDI import window, an audio file becomes the background waveform. False: not one of those.
    bool loadFile (const juce::File& file);
    // File › Open score (replaces the channel's notes in the pattern, FUN_00ff85c0) / Save score as (the channel's notes)
    bool openScore (const juce::File& file);
    bool saveScore (const juce::File& file) const;
    // File › Copy to / Paste from MIDI clipboard (a Standard MIDI File of the channel's notes; Ctrl+Shift+V pastes)
    bool copyToMidiClipboard() const;
    void pasteFromMidiClipboard();
    // File › Browse scores: the main window opens the Browser at FL's score folder
    std::function<void()> onBrowseScores;
    // "Note names on every key" was switched in the menu (the main window saves its settings)
    std::function<void()> onNoteNamesChanged;
    // round 190im (R13): set by the Clip View while this editor is docked for a clip of the grid: the clip's pattern
    // position while it is launched (−1: not playing); the playhead shows it
    std::function<double()> launchedPlayhead;

private:
    // round 190im (R15): what the frame step saw of the view (a change it did not make = a scroll by hand), and the
    // song's publish count at the last mouse press (a gesture that changed it = an edit)
    double followSeenScroll = 0.0, followSeenZoom = 0.0;
    std::uint64_t followEditStamp = 0;

public:
    // The play button: click plays / pauses the pattern, right-click stops back at the last start (the main window's transport)
    std::function<void()> onPlayPause, onPlayRightClick;
    // The time selection (Ctrl+drag on the ruler; -1 = none) that Insert / Delete space and Trim use.
    void setTimeSelection (std::int64_t from, std::int64_t to);
    std::int64_t timeSelectionFrom() const noexcept { return timeFrom; }
    std::int64_t timeSelectionTo() const noexcept { return timeTo; }
    std::vector<int> selectionForTests() const { return selection; }
    // The MIDI Tools panel (round 190s): the notes a tool works on (the selection, or every note of the channel when
    // nothing is selected: `selected` false), and its result becomes the selection.
    std::vector<int> midiToolNotes (bool& selected) const { selected = ! selection.empty(); return selectionOrAll(); }
    void selectMidiToolResult (std::vector<int> indices) { setSelection (std::move (indices)); }
    // round 190mc: notes another panel of this editor just added (the Chord helper's insert) become the selection; the
    // note count it made is this editor's own, not an outside change that drops the selection
    void selectOwnNewNotes (std::vector<int> indices);
    std::int64_t midiToolGrid() const { return gridTicks(); }
    void openMidiTools(); // Tools › MIDI Tools: the panel in its own small window
    // Tools › Chord helper (round 190mc, discussion #105): modal interchange from the selected chord, in its own small
    // window in the shown layout's look; it follows this editor's selection while it is open
    void openChordHelper();
    ChordHelperPanel* chordHelperForTests() const noexcept;
    std::int64_t markerKeyTickForTests() const { return markerKeyTick(); }
    juce::String chordBubbleTextForTests() const { return chordBubble.isVisible() ? chordBubble.getText() : juce::String(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;

    // For snapshots: the tool in use and the Note inspector of the selection.
    void setToolForSnapshot (EditTool tool) { tools.setTool (tool); }
    const ToolStrip& toolsForSnapshot() const noexcept { return tools; } // (round 190mp)
    juce::PopupMenu selectMenuForSnapshot() { return selectMenu(); }     // (round 190mp, DE-71)
    void setSelectionForSnapshot (const std::vector<int>& s) { setSelection (s); }
    struct NoteOnScreen
    {
        int index = -1;
        juce::Rectangle<int> bounds;
    };
    NoteOnScreen firstVisibleNoteForSnapshot (int skip = -1, juce::Point<int> near = {}) const; // (round 190mp: a note of the
                                                                                                 // channel wholly on the grid; with `skip`, the one nearest `near`)
    // --details190imshot (round 190im): one display-rate frame step, and where the playhead is (−1: none)
    void frameStepForSnapshot() { playheadFrame (0.0); }
    double playheadForSnapshot() const noexcept { return playheadTick; }
    EditTool currentTool() const noexcept { return tools.getTool(); } // (round 190iu: temporary tools)
    void setScaleForSnapshot (int root, int scale) { setScale (root, scale); }
    // round 190jv: View › Scale highlighting › Automatic on / off, and the key it guessed (scaledetect; -1 none)
    void setAutoScale (bool on) { scaleBox.setSelectedId (on ? autoScaleId : 1, juce::sendNotificationSync); }
    bool isAutoScaleForTests() const { return isAutoScale(); }
    int autoScaleKeyForTests() { updateAutoKey(); return isAutoScale() ? autoKey : -1; }
    juce::PopupMenu getViewMenuForSnapshot() { return viewMenu(); }
    juce::PopupMenu getScaleHighlightMenuForSnapshot() { return scaleHighlightMenu(); }
    // issue #203 (round 190ll): the toolbar's scale choice as shown — the FL layout's box and the menu its click opens;
    // docked, the root and scale boxes (their texts and item counts)
    bool scaleBoxesSplitForTests() const { return splitScaleBoxes(); }
    juce::String scaleTextForTests() const { return scaleBox.getText(); }
    juce::String scaleRootTextForTests() const { return scaleRootBox.getText(); }
    juce::String scaleNameTextForTests() const { return scaleNameBox.getText(); }
    int scaleNameItemsForTests() const { return scaleNameBox.getNumItems(); }
    void pickScaleRootForTests (int root) { scaleRootBox.setSelectedId (root + 1, juce::sendNotificationSync); }
    void pickScaleNameForTests (int itemIndex) { scaleNameBox.setSelectedItemIndex (itemIndex, juce::sendNotificationSync); }
    int scaleRootForTests() const { return scaleRoot(); }
    int scaleIndexForTests() const { return scaleIndex(); }
    juce::Rectangle<int> scaleBoundsForSnapshot() const
    {
        return splitScaleBoxes() ? scaleRootBox.getBounds().getUnion (scaleNameBox.getBounds()) : scaleBox.getBounds();
    }
    // The scale the rows show (LYNX's scale list index, 0 = none) on `root` (round 190ds: the Simple dock's "Only notes
    // in key" sets the project key here before folding)
    void setScale (int root, int scale) { scaleBox.setSelectedId (scale <= 0 ? 1 : scale * 100 + root + 1, juce::sendNotificationSync); }
    std::unique_ptr<juce::Component> makeNoteInspectorForSnapshot (bool singleNote = false);
    std::vector<int> noteIndicesForSnapshot() const { return selectionOrAll(); }
    std::int64_t gridForSnapshot() const { return gridTicks(); }
    juce::PopupMenu getToolsMenuForSnapshot() { return toolsMenu(); }
    juce::PopupMenu getScriptsMenuForSnapshot() { return scriptsMenu(); }
    // runs a Piano roll script by its menu name (LYNX's own, or a file of the script folders)
    void runScriptForSnapshot (const juce::String& name);
    juce::PopupMenu getStampMenuForSnapshot() { return stampMenu(); }
    void squeezeToolbarForSnapshot (int pixels); // the toolbar as in a window `pixels` narrower (negative: back)
    // zooms in (taller rows, wider steps) and hovers the right edge of the first note; returns its hint
    ui::Hint hoverForSnapshot();
    // round 190mf (DL-80): docked in Live's look, the first note's velocity marker hovered (or dragged); its value
    int liveVelocityHoverForSnapshot (bool dragging);
    // the lane box's note properties (ids; pattern event lanes follow from eventLaneIdBase)
    static constexpr int laneVelocity = 1, lanePan = 2, laneFinePitch = 3, laneRelease = 4, laneModX = 5, laneModY = 6, laneChance = 7,
                         laneVelocityRange = 8,
                         // MPE stage 2 (issue #108, core/NoteCurves.h): the notes' own pitch bend / slide / pressure curves
                         laneMpePitch = 9, laneMpeSlide = 10, laneMpePressure = 11, laneLast = 11;
    static constexpr bool isMpeLane (int id) noexcept { return id >= laneMpePitch && id <= laneMpePressure; }
    // The Clip-launch layout's MPE tab (Live 11+'s note expression editor, round 190hy): the bottom lane becomes the three
    // expression lanes stacked (pitch bend, slide, pressure) and the notes show their pitch curves
    void setMpeView (bool on);
    bool isMpeView() const noexcept { return mpeStacked; }
    // (tests / snapshots: the lane rectangles shown and the edit target at x)
    std::vector<std::pair<juce::Rectangle<int>, int>> mpeLaneAreasForTests() const { return mpeLaneAreas(); }
    // round 190jd (Live 12.4's MPE tab): the lanes its "Show Lane:" menu shows (core/MpeTab.h; a session preference)
    static inline mpetab::LaneSet mpeShownLanes = mpetab::defaultLanes;
    void setMpeLanesForSnapshot (mpetab::LaneSet lanes);
    juce::PopupMenu getMpeLaneMenuForSnapshot() { return mpeLaneMenu(); }
    // hovers the line of lane `laneId` at `tick` as the mouse would (the value ghost point); returns its text ("" = none)
    juce::String hoverMpeLineForSnapshot (int laneId, double tick);
    // the bottom lane's property (laneVelocity … laneVelocityRange; round 187c)
    void setLaneForSnapshot (int id) { laneBox.setSelectedId (id, juce::sendNotificationSync); }
    // The divider between the note grid and the event editor (round 190fz, FL's TQuickSplitter; core/EventPaneSplit.h):
    // its place is remembered for the Piano roll in the settings folder (FL: per window kind, not per project).
    static evpane::State& eventPaneState();   // (read from the settings file once)
    static void saveEventPaneState();          // (written after every change made with the mouse or the key)
    juce::Rectangle<int> eventPaneBarArea() const; // the bar (empty in the Clip-launch dock, whose lane is fixed)
    // the same steps as the mouse: move the bar's top to `y` (as a drag), swap with the alternate size, the default place
    void moveEventPaneBar (int y);
    void toggleEventPaneAlternate();
    void resetEventPane();
    int eventPaneBarTopForTests() const noexcept { return eventPaneBarTop; }
    // --undogesture190jv (round 190jv): the middle of the row of `key` at `tick`, in this component's coordinates
    // round 190mq (issues #211, #212, #213): the view and the time selection, for --issueshot190mq
    void setViewForTests (double firstTick, int rowPixels, int centreKey)
    {
        scrollTicks = firstTick;
        setRowHeightAround (rowPixels, centreKey, gridArea().getCentreY());
        repaint();
    }
    std::pair<std::int64_t, std::int64_t> timeSelectionForTests() const { return { timeFrom, timeTo }; }
    void setTimeSelectionForTests (std::int64_t from, std::int64_t to) { setTimeSelection (from, to); }
    int rulerYForTests() const { return rulerTop + timelineHeight / 2; }
    juce::Point<int> pointForTests (std::int64_t tick, int key) const { return { (int) tickToX ((double) tick), (keyToY (key) + keyToY (key - 1)) / 2 }; }

    static constexpr int keyboardWidth = 72;
    static constexpr int blackKeyWidth = keyboardWidth * 16 / 25; // FL's black keys: 64 % of the keyboard (round 190jo FM-17; was 3 / 5)
    static constexpr int toolbarHeight = 32; // the panel title row (Workspace::titleHeight)
    static constexpr int overviewHeight = 16; // the overview strip under the toolbar (round 90)
    static constexpr int timelineHeight = 22;
    static constexpr int rulerTop = toolbarHeight + overviewHeight;
    static constexpr int headerHeight = rulerTop + timelineHeight;
    static constexpr int defaultVelocityHeight = 72; // (before round 190fz the floating panel's fixed lane; now it follows the divider)
    static constexpr int compactVelocityHeight = 30; // docked in the Clip-launch detail view (round 183a): the reference's lane is ~30 px
    int velocityHeight = defaultVelocityHeight;
    // docked in the Clip-launch detail view (round 183a): a short lane, and the keys fitted to the notes when it scrolls to them;
    // the floating panel's key height comes back when it goes home
    void setDocked (bool docked)
    {
        if (fitKeysToNotes == docked)
            return;
        fitKeysToNotes = docked;
        if (! docked)
            mpeStacked = false; // (round 190hy: the MPE tab's stacked lanes are the dock's)
        velocityHeight = docked ? compactVelocityHeight : defaultVelocityHeight;
        if (docked)
            undockedRowHeight = rowHeight;
        else
            rowHeight = undockedRowHeight;
        resized();
        repaint();
    }
    bool fitKeysToNotes = false;
    // Round 190jn (design190ia CL-20b): docked in the Clip View in the Clip-launch layout's Live look, the notes take the
    // clip's colour as Live 12.4 draws them (colour × 0.86, a 1 px black edge) on Live's note-editor grey #464F53
    // (ableton190o live-01-clipview-notes.png); transparent = LYNX's note colours. Only the dock sets it.
    juce::Colour liveNoteColour { juce::Colours::transparentBlack };
    bool liveNotes() const;
    // Round 190hr (detail-055, miditools-098): docked in the Clip-launch detail view, the editor uses the reference's
    // grid (core/LiveGrid.h: adaptive / fixed, narrower / wider, triplet, snap; its value at the toolbar's grid place)
    // and its Quantize (Ctrl+U) / Quantize Settings (Ctrl+Shift+U), which the Clip View's MIDI Tools Quantize tool does
    // (Live 12: Quantize Settings… opens that tool; the dock sets the two hooks). The FL layout keeps FL's snap box. UI
    // state, not saved.
    livegrid::Grid liveGrid;
    std::function<void()> onLiveQuantize, onLiveQuantizeSettings;
    bool liveGridOn() const noexcept { return fitKeysToNotes; }
    juce::PopupMenu liveGridMenu();
    std::int64_t gridStepForCheck() const { return gridTicks(); }
    juce::String liveGridTextForCheck() const { return liveGridButton.getButtonText(); }
    // round 190jo (design190ia FM-15): FL's 18 px rows by default (was 14), notes 17 px with a 1 px row line under them
    int undockedRowHeight = 18;
    int rowHeight = 18; // vertical zoom (Ctrl+middle-drag, Ctrl+Alt+wheel): gesture::minKeyHeight..maxKeyHeight px per key
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, resizeLeft, erase, keyboard, select, velocity, velocityLine, velocitySet, paint, muting, knife,
                      zoomRange, listen, pan, keyZoom, timeZoom, keySelect, rulerSelect, seek, drumPaint, stretch, eventDraw, eventErase,
                      eventPaneBar, eventPaneAlternate, // (round 190fz: dragging the divider; a right-click on it, swapped on release)
                      mpePoint, mpeDraw, mpeErase, // (round 190hy: a curve point moved, drawn, erased in an MPE lane)
                      mpeBend, mpeValue }; // (round 190jd: a pitch point on a note dragged; a Velocity / Release Velocity lane set)

    // Channel selector: the wheel steps through the channels with notes (Ctrl+wheel: all channels).
    class ChannelBox final : public juce::ComboBox
    {
    public:
        std::function<void (int direction, bool all)> onStep;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (onStep && wheel.deltaY != 0.0f)
                onStep (wheel.deltaY < 0 ? 1 : -1, flmods::ctrl (e.mods));
        }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    // the playhead, at the display's refresh (round 174e; it was on the 30 Hz timer)
    bool playheadFrame (double nowMs);
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int yToKey (int y) const;
    int keyboardKeyAt (int x, int y) const; // the key a point of the keyboard plays (the keyboard style decides)
    int rowFor (int key) const noexcept;  // the key's row from the top (a folded-away key: the row above it)
    void updateKeyRows();                 // after the scale or the fold changed
    notetools::KeyRows rows = notetools::keyRows (0, 0, false);
    bool foldToScale = false;
    // round 190el (decision 2 of the 190ei Live check): in the Clip-launch layout a Drum Rack channel's editor shows a
    // row per filled pad, named, instead of the piano keys (Live's note editor for a Drum Rack, Fold on or off); the
    // Pattern layout keeps FL's piano roll. The filled pads' keys (highest first), empty = the usual rows.
    std::vector<int> padRows;
    std::vector<int> wantedPadRows() const;
    void syncPadRows(); // the rows follow the layout, the channel and its pads
    bool folded() const noexcept { return rows.count < 128; }
    bool ghostShown (int channel) const noexcept;
    void setGhostChannels (const std::function<bool (int channel, bool shown)>& show);
    std::int64_t gridTicks() const;        // the grid step in use (resolving Screen / Nearest note)
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t viewLengthTicks() const;
    std::int64_t newNoteLength() const;
    // round 190jo (FM-15): a note's box, FL's anatomy: the whole row but the 1 px line under it, from its start to 1 px
    // before its end (at least 4 px wide)
    juce::Rectangle<float> noteRect (std::int64_t start, std::int64_t length, int key) const
    {
        return { (float) tickToX ((double) start), (float) keyToY (key), juce::jmax (4.0f, (float) (length * pixelsPerTick) - 1.0f),
                 (float) juce::jmax (2, rowHeight - 1) };
    }
    int noteAt (int x, int y, bool& onRightEdge) const;
    int noteAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const;
    int ghostAt (int x, int y) const;      // index of a ghost note (another channel) under the point
    void refreshChannelBox();
    void stepChannel (int direction, bool withNotesOnly);
    void zoomToTicks (double from, double to);
    void zoomIn (int anchorX);
    void zoomOut (int anchorX);
    void listenAt (int x);
    void seekAt (int x, bool snapOn); // the ruler: moves the position (Alt: off the grid)
    void stopListening();
    void cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn);
    void paintNoteAt (std::int64_t start, int key, bool monophonic);
    void runOperation (const std::function<std::vector<int> (Pattern&, const std::vector<int>&)>& op);
    void showMoreMenu();
    juce::PopupMenu moreMenu(); // (the menu showMoreMenu shows)
    void updateScrollBars();
    void previewOn (int key, float velocity);
    // previewOn for a note the mouse placed, dragged or edited: silent while playing unless previewWhilePlaying (#183)
    void previewEdit (int key, float velocity);
    float keyVelocityAt (int x, int key) const; // velocity of a click on the keyboard at x
    void previewOff();
    void visibilityChanged() override;

    bool isSelected (int index) const;
    std::vector<int> channelNotes() const;           // indices of the channel's notes
    std::vector<int> selectionOrAll() const;         // selection, or all of the channel's notes if empty
    void setSelection (std::vector<int> indices);
    void sortNotes();                                // sorts while keeping the selection (no undo step)
    void editNotes (const std::function<void (Pattern&)>& edit, bool undoable = true);
    // Freeze (round 190ay): the notes of a frozen channel are read-only. True = the channel shown is frozen; the hint
    // bar says "Unfreeze to edit" (the edit is refused, not silently dropped).
    bool refuseFrozenEdit();
    void applyVelocity (int fromX, int toX, int y);
    float laneValue (const Note& n) const;          // 0..1 of the chosen property
    void setLaneValue (Note& n, float value) const; // inverse
    bool laneIsBipolar() const;
    bool laneIsChance() const;
    void addNoteLanes();
    void setNotesMuted (bool mute); // the selection (or all notes)
    void togglePorta(); // porta notes glide from the previous note (toggles the channel's porta)

    void deleteSelection();
    void copySelection();
    std::vector<Note> visibleGhostNotes() const; // round 177d: the ghost notes on show, as notes (song pattern ticks)
    void copyGhostNotes();
    void paste();
    std::int64_t pasteOffset() const; // issue #211: how far Ctrl+V moves the clipboard (core/PastePlace.h)
    void duplicateSelection();
    void nudge (std::int64_t ticks, int keys);

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true }; // hBar is hidden: the overview strip scrolls sideways
    OverviewBar overview;
    void paintOverview (juce::Graphics& g, juce::Rectangle<float> area, double total);
    std::size_t overviewSignature = 0; // the notes drawn in the overview (repaints it when they change)
    // round 190gp: the song stamp, pattern and channel the signature was last worked out for (a paint — each playhead
    // column too — hashed every note of the pattern; the notes change only with the song, whose stamp moves then)
    std::uint64_t overviewStamp = 0;
    int overviewPattern = -1, overviewChannel = -1;
    ToolStrip tools { false };
    int titleInsetLeft = 0, titleInsetRight = 0; // the window's title and buttons share the toolbar row
    ChannelBox channelBox;
    SnapBox snapBox { true };         // "Main" (the toolbar snap) or this editor's own grid
    juce::TextButton liveGridButton;  // round 190hr: docked, the reference's grid value ("1/16 ▾") and its menu
    IconButton ghostButton { "ghosts", Icon::ghost };
    IconButton scaleSnapButton { "scaleSnap", Icon::scaleSnap }; // snap to the scale (next to the scale box)
    IconButton moreButton { "more", Icon::more }; // note operations, left-edge resize, MIDI import/export
    IconButton playButton { "play", Icon::play }; // FL's "Play / pause pattern" (EEPlayBtn); right-click: back to the last start
    IconButton followButton { "follow", Icon::follow }; // Follow (auto scrolling) on/off, EditorGlobals (round 162)
    void quantizeOperation (int kind); // 0 = starts, 1 = starts and lengths (quick quantize), 2 = starts 50% toward the grid
    juce::ComboBox laneBox; // what the bottom lane edits: velocity, pan or fine pitch
    IconComboBox chordBox { Icon::chord }; // chord created on each click (like FL's "stamp")
    IconButton slideButton { "slide", Icon::slide }; // new notes slide (like FL's slide tool)
    // scale highlighted on the keyboard rows. Issue #203 (round 190ll): its list held every scale on each of the 12
    // roots (C Major, C# Major, ... B Locrian). In the FL layout a click opens FL's View › Scale highlighting menu
    // instead (its "Root note" and "Scale" sections: one root, one scale); docked in the Clip-launch and Simple layouts
    // the root and the scale are two boxes side by side, as Live's Clip View "Scale" (root | scale name) and BandLab's
    // Smart View (key | scale). This box stays the one place the choice lives (its ids, the saved data, the tests).
    class ScaleBox final : public IconComboBox
    {
    public:
        using IconComboBox::IconComboBox;
        std::function<void()> onPopup;
        void showPopup() override
        {
            if (onPopup)
                onPopup();
            else
                IconComboBox::showPopup();
        }
    };
    ScaleBox scaleBox { Icon::keyboard };
    juce::ComboBox scaleRootBox, scaleNameBox; // (issue #203: docked in the Clip-launch / Simple layouts)
    int splitRoot = 0;                         // the root box's choice while no scale is chosen
    bool splitScaleBoxes() const noexcept { return fitKeysToNotes; }
    void refreshScaleBoxes();
    // note colour groups (FL's 16 note colours; REA round 153): new notes get `noteColour`; the swatch opens a menu
    // that picks it, colours the selection and selects by colour
    class ColourSwatch final : public juce::Button
    {
    public:
        ColourSwatch() : juce::Button ("noteColour") {}
        int colourIndex = 0;
        void paintButton (juce::Graphics& g, bool over, bool down) override;
    };
    ColourSwatch colourButton;
    int noteColour = 0;
    void showColourMenu();
    void colourSelection (int colour);   // one undo step
    void selectByColour (int colour);    // the channel's notes of that colour
    juce::PopupMenu selectMenu();        // FL's Select submenu (invert, odd, random, muted, overlapping, stacked, colour)
    // chord readout (FL 2026's chord detection): the chord of the held MIDI / typing-piano keys, else of the
    // selected notes
    juce::Label chordLabel;
    void updateChordReadout();
    void showKeyMenu (int key); // right-click on the keyboard: key labels of the channel
    std::vector<int> chordIntervals() const;
    bool chordTopDown = false; // the chord stamp puts the clicked key on top (FL 2026's top-down mode)
    bool inScale (int key) const;
    int scaleRoot() const;  // 0..11
    int scaleIndex() const; // index into notetools::scales(), 0 = none (also Automatic)
    // FL's View › Scale highlighting › Automatic (round 190jv, core/ScaleDetect.h): the scale box's item, the key guessed
    // from the edited notes (scaledetect: 0..11 major, 12..23 minor, -1 none) and what it was guessed from
    static constexpr int autoScaleId = 2; // (between "No scale" = 1 and the scales, s * 100 + root + 1)
    bool isAutoScale() const { return scaleBox.getSelectedId() == autoScaleId; }
    void updateAutoKey();
    int autoKey = -1;
    bool autoKeyValid = false, autoKeyAllChannels = false;
    std::uint64_t autoKeyStamp = 0;
    int autoKeyPattern = -1, autoKeyChannel = -1;
    std::vector<int> autoKeySelection;
    juce::PopupMenu scaleHighlightMenu(); // View › Scale highlighting
    int snapKey (int key) const; // the key itself, or the nearest in-scale key while scale snap is on
    scalesnap::Scale snapScale() const; // the chosen scale and the Snap to scale switch (core/ScaleSnap.h)
    void publishScale();
    bool scaleSnap = false;
    double panStartTicks = 0.0;
    int panStartY = 0;
    void zoomAroundCentre (double factor);
    void zoomToBars (int bars);
    void zoomToSelection();
    void zoomAll();
    void centreOnPlayhead();
    juce::PopupMenu toolsMenu();

    // --- scripts (Tools › Scripts: FL's Piano roll scripts, round 190as; ui/PianoRollScripts.cpp) ---------------------
    struct ScriptRef
    {
        juce::String name;  // menu name
        juce::String key;   // the form memory's key (the script's path, or "lynx:<name>")
        std::string source; // a built-in script's text (a file is read when it runs)
        juce::File file;
    };
    struct ScriptJob;
    struct ScriptResult;
    juce::PopupMenu scriptsMenu();
    void addScriptItems (juce::PopupMenu& menu);
    void runScript (const ScriptRef& ref);
    void runLastScript();
    void scriptStep (std::shared_ptr<ScriptJob> job, std::vector<scriptform::Form> answers, bool preview);
    void scriptFinished (std::shared_ptr<ScriptJob> job, ScriptResult& result, bool preview, std::uint64_t ticket);
    bool scriptTargetStill (const ScriptJob& job) const;
    void showScriptPattern (const Pattern& pattern, const std::vector<int>& select, bool undoable);
    void commitScript (const ScriptJob& job, const Pattern& result, const std::vector<int>& select, std::int64_t from, std::int64_t to, bool takeTimeSelection);
    void showScriptText (const juce::String& title, const juce::String& text);
    ScriptRef lastScript;
    bool scriptBusy = false;
    std::uint64_t scriptTicket = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::PopupMenu midiMenu();

    // hover: what a click would hit (drives the cursor, the highlight and the hint)
    struct Hover
    {
        int note = -1, ghost = -1, key = -1;
        gesture::Zone zone = gesture::Zone::outside;
        bool stretch = false; // the selection's stretch handle
        bool operator== (const Hover& o) const
        {
            return note == o.note && ghost == o.ghost && key == o.key && zone == o.zone && stretch == o.stretch;
        }
    };
    Hover hover;
    juce::Point<int> lastMouse { -1, -1 };
    // round 190mf (DL-80): the marker whose value Live's lane header shows (set while the lane paints)
    struct LiveVelocityReadout
    {
        int note = -1;
        float y = 0.0f;
        int value = 0;
    };
    mutable LiveVelocityReadout liveVelocityReadout;
    bool liveLaneHover = false;
    Hover hoverAt (juce::Point<int> p) const;
    gesture::Target targetAt (const Hover& h) const;
    void updateHover (juce::Point<int> p, juce::ModifierKeys mods);
    void updateCursor (juce::ModifierKeys mods);
    void applyLaneValue (int x, float value, int fromX); // sets the lane value of the notes between fromX and x
    // Alt+wheel (FL 2026, round 166e): `note` < 0 = the selection, else that note alone; the note is heard (with the
    // selection: the selected note nearest to `mouse`), and a single note's properties become the new-note template
    void wheelNoteProperty (int note, float deltaY, bool fine, juce::Point<int> mouse);
    noteedit::NoteTemplate noteTemplate; // FL: the properties new notes get from the last clicked note (round 166e)
    juce::uint32 wheelPreviewSerial = 0;
    void postDragHint (const juce::MouseEvent& e); // what the gesture in progress does (hint bar)
    juce::uint32 lastWheelEdit = 0;    // wheel edits within a burst are one undo step
    std::vector<Note> laneOriginals;   // right-drag line / Shift+drag: the pattern's notes when it started

    // MPE lanes (round 190hy, issue #108; core/NoteCurves.h). Lane ids are laneMpePitch … laneMpePressure.
    bool mpeStacked = false;           // the Clip-launch MPE tab: three lanes stacked
    int mpeDragLane = 0, mpeDragNote = -1, mpeDragPoint = -1, lastMpeX = 0, lastMpeY = 0;
    float mpeDragRange = 1200.0f;      // the pitch lane's half range (cents) when the drag started (kept while it lasts)
    juce::uint32 mpeCreatedAt = 0;     // a click that drew a point: the double click that follows does not delete it
    std::vector<std::pair<juce::Rectangle<int>, int>> mpeLaneAreas() const; // (area, lane id) shown now; none = not an MPE lane
    int mpeLaneAt (juce::Point<int> p, juce::Rectangle<int>* area = nullptr) const;
    float mpePitchRange() const;       // half range of the pitch lane: ±12, ±24 or ±48 semitones, as the curves need
    float mpeNorm (int lane, float value, float range) const noexcept;  // a curve value -> 0..1 of the lane's height
    float mpeValue (int lane, float norm, float range) const noexcept;  // and back
    static float mpeBase (int lane, const Note& n) noexcept;           // the lane's value without a curve
    int mpeNoteAt (int x) const;       // the note an MPE lane edits at x: a selected one first, else the latest started
    int mpePointAt (int note, int lane, juce::Rectangle<int> area, juce::Point<int> p, float range) const;
    void paintMpeLane (juce::Graphics& g, juce::Rectangle<int> area, int lane, float range);
    void paintMpePitchOnNotes (juce::Graphics& g, juce::Rectangle<int> grid);
    bool mpeMouseDown (const juce::MouseEvent& e);
    bool mpeMouseDrag (const juce::MouseEvent& e);
    bool mpeDoubleClick (const juce::MouseEvent& e);
    void editNoteCurves (int note, const std::function<void (Note&)>& edit);
    // round 190jd, the MPE tab as Live 12.4's (core/MpeTab.h): pitch on the notes, Slide / Pressure / Velocity / Release
    // Velocity lanes from the "Show Lane:" menu, click-on-the-line points, a value ghost point on hover
    int mpeFocusLane = laneMpePressure; // the footer names the lane edited last (Live: "Pressure", then "Slide")
    float mpeBendStart = 0.0f;          // the pitch point's cents when its drag started
    int mpeBendY0 = 0;
    juce::Point<float> mpeGhost { -1.0f, -1.0f }; // the value ghost point under the mouse (x < 0: none)
    juce::String mpeGhostText;
    static int mpeLaneIdOf (mpetab::Lane lane) noexcept;
    juce::Rectangle<int> mpeFooterArea() const;   // under the stacked lanes: ▾ "Show Lane" + the focused lane's name
    juce::Rectangle<int> mpeGhostBounds() const;
    juce::PopupMenu mpeLaneMenu();
    mpetab::HitResult mpeLaneHit (int note, int lane, juce::Rectangle<int> area, juce::Point<int> p, float range) const;
    std::pair<int, int> mpePitchPointAt (juce::Point<int> p) const; // (note, point) of a selected note's pitch curve
    bool mpePitchMouseDown (const juce::MouseEvent& e, int hit, bool onEdge);
    bool mpePitchDoubleClick (const juce::MouseEvent& e);
    void updateMpeGhost (juce::Point<int> p);
    void paintMpeValueLane (juce::Graphics& g, juce::Rectangle<int> area, int lane);
    void paintMpeTabFrame (juce::Graphics& g);    // the stacked lanes' left column (icon, scale) and the footer
    static float mpeValueLaneValue (int lane, const Note& n) noexcept;
    static void setMpeValueLaneValue (int lane, Note& n, float v) noexcept;
    float laneSetValue = 0.0f;
    // pattern event lanes in the lane box (ids eventLaneIdBase + k; REA round 146)
    static constexpr int eventLaneIdBase = 100;
    std::vector<AutomationTarget> eventTargets;
    void rebuildLaneBox (std::optional<AutomationTarget> shown = std::nullopt, bool force = false);
    void drawEvents (int fromX, int toX, int y, bool erase);
    int keyZoomKey = 60, keyZoomStartRow = 14, keyZoomCentreY = 0; // the key whose centre stays at keyZoomCentreY
    gesture::HeightZoomDrag keyZoomDrag;
    gesture::SpanZoomDrag timeZoomDrag;
    void beginKeyZoom (const juce::MouseEvent&);
    // round 190j: the zoom wheel (FL's VZoomSelect) at the right end of the overview strip
    static constexpr int zoomWheelSize = 16;
    magiclasso::Trail lassoPath; // FL's Magic lasso (round 190mp, DE-71): the mouse trail while the option is on
    bool zoomWheelHover = false; // (round 190he, issue #152 "no vertical zoom found": the box lights up under the mouse)
    juce::Rectangle<int> zoomWheelArea() const;
    int zoomAnchorKey() const; // the key whose centre stays put: the middle of the notes in view, else of the view
    void setRowHeightAround (int height, int key, int centreY);
    void beginTimeZoom (const juce::MouseEvent&);

    // smooth wheel scrolling
    gesture::Glide glideX, glideY;
    bool gliding = false;
    // `immediate`: a trackpad's smooth scroll moves the view at once (the fingers carry it; round 170e)
    void glideBy (double ticks, double pixels, bool immediate = false);
    void glideFrame();
    ui::OnDemandVBlank vblank { *this, [this] { glideFrame(); return gliding; } }; // (attached only while gliding)
    ui::FrameTicker frames { *this, [this] (double ms) { return playheadFrame (ms); } };
    // round 190ip: while playing, the GPU moves the playhead over the view (ui/MotionOverlay.h) instead of repainting its
    // column every frame; stopped (or when the overlay cannot) it is painted as before
    void paintPlayheadAt (juce::Graphics& g, float x);
    ui::MotionOverlay gpuPlayhead { *this, [this] (juce::Graphics& g, float x) { paintPlayheadAt (g, x); } };
    ui::MotionOverlay::Mark playheadMark() const;
    juce::Rectangle<int> playheadColumn (double tick) const;

    double pixelsPerTick = 24.0 / ticksPerStep; // 24 px per step
    double scrollTicks = 0.0;
    int scrollY = (127 - 84) * rowHeight;       // starts showing roughly C6..C3

    Drag drag = Drag::none;
    int dragNote = -1;              // grabbed note (reference for snap and preview)
    std::vector<int> dragIndices;   // notes that follow the drag
    std::vector<Note> dragOriginals;
    std::int64_t grabOffset = 0;    // note tick - mouse tick at drag start
    std::int64_t lastLength = ticksPerStep * 2;
    int previewKey = -1;
    int lastScaleDragKeys = 0; // (a snap-to-scale drag: the scale steps last applied)
    NoteMask litKeys {}; // keys sounding on the channel(s) shown: lit on the keyboard (issue #14, FL)
    NoteMask soundingNow() const;
    bool isLit (int key) const noexcept { return key >= 0 && key < 128 && ((litKeys[(std::size_t) (key >> 6)] >> (key & 63)) & 1u) != 0; }
    double playheadTick = -1.0;
    juce::Rectangle<int> liveHeldArea; // round 177d: where the held notes of a take were last drawn

    std::vector<int> selection;     // indices into Pattern::notes, sorted
    std::vector<int> selectionBefore; // selection at the start of a Shift+rectangle
    juce::ModifierKeys downMods;      // modifiers at the click (locks start when Shift/Ctrl are pressed later)
    // the channel box's bold "All" (issue #12): every channel's notes in the pattern are editable at once (new notes
    // still go to the selected channel); off again when another channel is picked
    bool allChannels = false;
    static constexpr int allChannelsId = 100000; // its id in channelBox
    bool edits (const Note& n) const noexcept { return allChannels || n.channel == engine.getSelectedChannel(); }
    void setAllChannels (bool on);
    bool showGhosts = true;
    bool editGhosts = false;          // Ctrl+Alt+V: a press on a ghost note edits it (switching to its channel)
    // the selection's stretch handle (R5): drag it to scale the selected notes in time
    juce::Rectangle<float> stretchHandle() const;
    std::int64_t stretchAnchor = 0, stretchEnd = 0;
    // drum brush: lays one note per grid cell and key
    void drumPaintAt (juce::Point<int> p, bool snapOn);
    std::int64_t lastDrumCell = -1;
    int lastDrumKey = -1;
    bool resizeFromLeft = true;       // the left edge of a note resizes it (Ctrl+Alt+Home turns it off)
    bool startedOnEmpty = false, erasedSomething = false, holdExpired = false;
    // Round 190mr (DE-63; capture 190mr A1.2): FL's click-and-hold functions (F10 › General › Input › "Click-and-hold &
    // special gesture functions", core/FlSettings.h clickHold, off as in FL). A press that stays still: from 390 ms a
    // thin ring grows clockwise from 12 o'clock at the mouse, and at 750 ms — on a note: a clone one grid step later is
    // what the mouse drags; on a note just added: slide, then every 750 ms porta, plain, slide…; at the resize cursor
    // between two touching notes: they are glued (and selected). A one-shot timer per press; the ring repaints on the
    // view's own timer only while a hold is pending.
    enum class Hold { none, copy, cycle, glue };
    Hold hold = Hold::none;
    int holdSerial = 0, holdStep = 0, glueA = -1, glueB = -1;
    juce::Point<int> holdDownAt, holdRingAt;
    juce::uint32 holdSince = 0;
    bool holdRing = false; // (the ring shows until the first action)
    static constexpr int holdMs = 750, holdRingFromMs = 390;
    void armHold (Hold kind, juce::Point<int> screenAt, bool ring = true);
    bool holdReadsMouse = true; // (--details190mrshot: the hold fires without a real mouse)
    void holdFired (int serial);
    juce::Rectangle<int> holdRingArea() const { return juce::Rectangle<int> (20, 20).withCentre (holdRingAt); }
    juce::uint32 createdAt = 0;       // when the last click created a note (a double click there is not an edit)
    std::int64_t nextPaintTick = 0, paintLength = 0;
    bool muteTarget = false;
    std::vector<int> touchedNotes;    // notes already muted/unmuted during this drag
    juce::Point<int> knifeFrom, knifeTo;
    std::vector<int> listenKeys;
    int listenChannel = -1;
    HeldNotes previewNotes;           // the note a click/drag previews (keyboard, notes); stops on release
    HeldNotes listenNotes;            // the Listen tool's notes under the mouse
    std::vector<std::int64_t> snapCandidates() const;
    juce::Rectangle<int> rubberBand;
    int lastVelocityX = 0;
    // round 190fz: the divider's top (the grid ends there, the event editor starts barThickness below) and where the
    // mouse took it
    int eventPaneBarTop = 0, eventPaneGrab = 0;
    void layoutEventPane(); // the grid / bar / lane heights from the remembered place and the window's height
    void eventPaneChanged(); // after a change: lay out, repaint, show the percentage, remember it
    int knownPattern = -1, knownChannel = -1;
    std::size_t knownNoteCount = 0;

    static inline std::vector<Note> clipboard;

    // round 168g
    std::int64_t timeFrom = -1, timeTo = -1;   // the time selection (ticks; -1 = none)
    std::int64_t rulerMark = -1; // issue #211 (round 190mq): the last single click on the ruler, FL's paste anchor; -1 = none
    juce::Label chordBubble;                   // "Show chord name for selection"
    juce::uint32 chordBubbleUntil = 0;
    std::vector<int> chordBubbleSelection;     // the selection it was shown for
    std::vector<int> chordPanelSelection;      // the selection the toolbar's chord panel last heard of
    juce::Component::SafePointer<juce::Component> chordHelper; // the open Chord helper (round 190mc)
    void updateChordBubble();
    void paintBackgroundWave (juce::Graphics& g, juce::Rectangle<int> grid);
    void spaceOperation (int kind);            // 0 insert, 1 slice & insert, 2 delete, 3 trim
    void insertControllerValue();              // Ctrl+I: the event lane's control at the song position
    void deleteAllNotes();
    void importMidi (const midiimport::ParsedMidi& midi, const juce::String& name);
    juce::PopupMenu fileMenu();
    juce::PopupMenu editMenu();
    juce::PopupMenu viewMenu();
    juce::PopupMenu stampMenu(); // the chord stamp's list (the toolbar chord box's items)
    // FL's Piano roll › Time markers on the pattern's own markers (round 168g)
    juce::PopupMenu markersMenu();
    std::int64_t markerKeyTick() const;   // Alt+T / Ctrl+T: the time selection's start, the playhead in view, else a bar
    void editPatternMarker (std::int64_t tick, int existing); // the name dialog (existing < 0: a new marker)
    void addPatternSignatureByKey();
    int markerAtX (int x) const;          // the pattern marker drawn under x on the ruler, or -1
    void showPatternMarkerMenu (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
    std::unique_ptr<ui::TimerWhileShowing> showingTimer; // the polling timer, only while on screen (round 174f)
};
} // namespace lynx
