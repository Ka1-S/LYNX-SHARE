#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/NoteTools.h"
#include "core/PianoKeys.h"
#include "core/ScaleSnap.h"
#include "engine/AudioEngine.h"
#include "ui/TimerWhileShowing.h"
#include "ui/EditorTools.h"
#include "ui/FrameTicker.h"
#include "ui/FlMods.h"
#include "ui/NoteToolDialogs.h"
#include "ui/ValueControl.h"
#include "core/NoteEdit.h"
#include "core/MidiImport.h"
#include "core/ScriptDialog.h"

namespace lynx
{
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
    // The shortcut text of a key map action for menus ("\tCtrl+Q"; empty when unbound), set by the main window.
    static inline std::function<juce::String (const std::string& id)> shortcutText;

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
    // View › Keyboard style (round 190j, discussion #136; LYNX's own): flat = FL's keyboard (the default), piano = a
    // traditional keyboard with a line between every two white keys (core/PianoKeys.h). Saved with the UI options.
    static inline pianokeys::Style keyStyle = pianokeys::Style::flat;
    void setKeyStyle (pianokeys::Style style);
    static inline bool songBarsRuler = false; // round 177d: the ruler numbers song bars (off = FL: pattern bars)
    // FL's General › "Monophonic painting in piano roll" (round 189o, off): on, the Paint tool paints monophonically
    // without Shift (noteedit::monophonicPaint). Saved with the UI options.
    static inline bool monophonicPainting = false;
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
    std::int64_t midiToolGrid() const { return gridTicks(); }
    void openMidiTools(); // Tools › MIDI Tools: the panel in its own small window
    std::int64_t markerKeyTickForTests() const { return markerKeyTick(); }
    juce::String chordBubbleTextForTests() const { return chordBubble.isVisible() ? chordBubble.getText() : juce::String(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped (const juce::DragAndDropTarget::SourceDetails& details) override;

    // For snapshots: the tool in use and the Note inspector of the selection.
    void setToolForSnapshot (EditTool tool) { tools.setTool (tool); }
    void setScaleForSnapshot (int root, int scale) { setScale (root, scale); }
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
    // the lane box's note properties (ids; pattern event lanes follow from eventLaneIdBase)
    static constexpr int laneVelocity = 1, lanePan = 2, laneFinePitch = 3, laneRelease = 4, laneModX = 5, laneModY = 6, laneChance = 7,
                         laneVelocityRange = 8, laneLast = 8;
    // the bottom lane's property (laneVelocity … laneVelocityRange; round 187c)
    void setLaneForSnapshot (int id) { laneBox.setSelectedId (id, juce::sendNotificationSync); }

    static constexpr int keyboardWidth = 72;
    static constexpr int toolbarHeight = 32; // the panel title row (Workspace::titleHeight)
    static constexpr int overviewHeight = 16; // the overview strip under the toolbar (round 90)
    static constexpr int timelineHeight = 22;
    static constexpr int rulerTop = toolbarHeight + overviewHeight;
    static constexpr int headerHeight = rulerTop + timelineHeight;
    static constexpr int defaultVelocityHeight = 72;
    static constexpr int compactVelocityHeight = 30; // docked in the Clip-launch detail view (round 183a): the reference's lane is ~30 px
    int velocityHeight = defaultVelocityHeight;
    // docked in the Clip-launch detail view (round 183a): a short lane, and the keys fitted to the notes when it scrolls to them;
    // the floating panel's key height comes back when it goes home
    void setDocked (bool docked)
    {
        if (fitKeysToNotes == docked)
            return;
        fitKeysToNotes = docked;
        velocityHeight = docked ? compactVelocityHeight : defaultVelocityHeight;
        if (docked)
            undockedRowHeight = rowHeight;
        else
            rowHeight = undockedRowHeight;
        resized();
        repaint();
    }
    bool fitKeysToNotes = false;
    int undockedRowHeight = 14;
    int rowHeight = 14; // vertical zoom (Ctrl+middle-drag, Ctrl+Alt+wheel): gesture::minKeyHeight..maxKeyHeight px per key
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, resizeLeft, erase, keyboard, select, velocity, velocityLine, velocitySet, paint, muting, knife,
                      zoomRange, listen, pan, keyZoom, timeZoom, keySelect, rulerSelect, seek, drumPaint, stretch, eventDraw, eventErase };

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
    void updateScrollBars();
    void previewOn (int key, float velocity);
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
    void duplicateSelection();
    void nudge (std::int64_t ticks, int keys);

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true }; // hBar is hidden: the overview strip scrolls sideways
    OverviewBar overview;
    void paintOverview (juce::Graphics& g, juce::Rectangle<float> area, double total);
    std::size_t overviewSignature = 0; // the notes drawn in the overview (repaints it when they change)
    ToolStrip tools { false };
    int titleInsetLeft = 0, titleInsetRight = 0; // the window's title and buttons share the toolbar row
    ChannelBox channelBox;
    SnapBox snapBox { true };         // "Main" (the toolbar snap) or this editor's own grid
    IconButton ghostButton { "ghosts", Icon::ghost };
    IconButton scaleSnapButton { "scaleSnap", Icon::scaleSnap }; // snap to the scale (next to the scale box)
    IconButton moreButton { "more", Icon::more }; // note operations, left-edge resize, MIDI import/export
    IconButton playButton { "play", Icon::play }; // FL's "Play / pause pattern" (EEPlayBtn); right-click: back to the last start
    IconButton followButton { "follow", Icon::follow }; // Follow (auto scrolling) on/off, EditorGlobals (round 162)
    void quantizeOperation (int kind); // 0 = starts, 1 = starts and lengths (quick quantize), 2 = starts 50% toward the grid
    juce::ComboBox laneBox; // what the bottom lane edits: velocity, pan or fine pitch
    IconComboBox chordBox { Icon::chord }; // chord created on each click (like FL's "stamp")
    IconButton slideButton { "slide", Icon::slide }; // new notes slide (like FL's slide tool)
    IconComboBox scaleBox { Icon::keyboard }; // scale highlighted on the keyboard rows
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
    int scaleIndex() const; // index into notetools::scales(), 0 = none
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
    juce::VBlankAttachment vblank { this, [this] { glideFrame(); } };
    ui::FrameTicker frames { *this, [this] (double ms) { return playheadFrame (ms); } };

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
    int knownPattern = -1, knownChannel = -1;
    std::size_t knownNoteCount = 0;

    static inline std::vector<Note> clipboard;

    // round 168g
    std::int64_t timeFrom = -1, timeTo = -1;   // the time selection (ticks; -1 = none)
    juce::Label chordBubble;                   // "Show chord name for selection"
    juce::uint32 chordBubbleUntil = 0;
    std::vector<int> chordBubbleSelection;     // the selection it was shown for
    std::vector<int> chordPanelSelection;      // the selection the toolbar's chord panel last heard of
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
