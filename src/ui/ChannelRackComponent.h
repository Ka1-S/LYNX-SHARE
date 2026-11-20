#pragma once

#include <array>
#include <atomic>

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ActivityLight.h"
#include "core/HeldNotes.h"

#include "core/RackOps.h"
#include "core/RackZip.h"
#include "engine/AudioEngine.h"
#include "ui/TimerWhileShowing.h"
#include "ui/FrameTicker.h"
#include "ui/ValueControl.h"
#include "ui/WrapperMenu.h"

namespace lynx
{
// The menu name of an FL instrument engine (core/NativeEngines.h, round 142, issue #5; the generator menus list them
// through quickpick::generatorEntries) ("(beta)", "(requires FL Studio)")
juce::String flEngineName (const natives::InstrumentEngine& e);

// FL-style Channel Rack: one row per channel with mute, pan, volume, mixer insert, name, selector
// and the step grid of the current pattern.
//  - steps: left click turns on (drag paints), right click erases
//  - name (channel button): click opens/closes the channel's editor (plugin window or settings),
//    closing the previously opened one unless Alt is held (or "keep editors open" is on);
//    Ctrl+click auditions; double click opens the Piano Roll; right click opens the channel menu;
//    dragging it up/down reorders the selected channels
//  - selector: click selects one row, Ctrl+click or right click toggles, Shift+click selects a range,
//    double click selects all; left-drag selects a range, right-drag adds the rows it crosses.
//    The selection receives MIDI input and is what the keys act on
//  - knobs, route box: the shared control contract (ui/ValueControl.h), with a value read-out while
//    dragging; double-click on the route box opens the Mixer at that insert
//  - everything under the mouse is highlighted and described in the hint bar
//  - dropping an audio file onto a row turns the channel into a sampler
class ChannelRackComponent final : public juce::Component,
                                   public juce::FileDragAndDropTarget,
                                   public juce::DragAndDropTarget,
                                   public ui::HintSource,
                                   private juce::ChangeListener,
                                   private juce::Timer
{
public:
    // LYNX extra (on by default; Options › General "Playhead column in the Channel Rack"): a soft highlight of the
    // playing step's column under every row's steps, and the first step of each bar marked on the playing-step strip
    static inline std::atomic<bool> playheadColumn { true };
    // LYNX extra (on by default; Options › General "Steps use channel colour"): a lit step in its channel's colour
    // (themes::litStepColour); FL's steps stay white / pink ("Colorful channel controls" only colours the insert box)
    static inline std::atomic<bool> stepsChannelColour { true };
    // FL's rack option "Show complete piano roll preview" (feedback 190bm): a row's miniature shows the whole pattern
    // in the visible width when the pattern is longer than the rack shows; off: step-aligned, it scrolls with the steps
    static inline std::atomic<bool> completePreview { true };
    explicit ChannelRackComponent (AudioEngine& engine);
    ~ChannelRackComponent() override;
    void visibilityChanged() override;

    std::function<void()> onOpenPianoRoll;
    // FL's piano roll preview (manual "Channel Rack › Piano roll preview", round 190mk DE-33): "Left-click the preview to
    // show/hide the Piano roll window"; "Right-Click to open the Piano roll centered on the notes clicked" (the tick)
    std::function<void (int row)> onPreviewClick;
    std::function<void (int row, double tick)> onPreviewRightClick;
    std::function<void (int)> onOpenMixerInsert; // double-click on a route box: show the Mixer at that insert

    // Settings > "Keep channel editors open": opening an editor never closes the previous one.
    static bool keepEditorsOpen;
    // FL's "Select first channel with notes when the pattern changes" (Settings › General; REA round 167j §5)
    static bool selectFirstChannelWithNotes;
    // FL's Settings › General › "Auto select linked modules" (AutoSelectBox, on; round 187a): a focused channel or
    // plugin window selects its channel and mixer track, and selecting one channel selects its mixer track
    static bool autoSelectLinked;
    // FL's Channel Rack option "Mute removed steps": turning a step off mutes its note instead of deleting it
    static bool muteRemovedSteps;
    // FL 2026's Channel rack menu › Options › "Show mixer track selectors" (Alt+M; round 190ld, DE-26): the mixer track
    // number box beside each channel's knobs; off = the column goes. Saved ("rackMixerSelectors")
    static inline bool showMixerSelectors = true;
    // FL's Channel rack menu › Options › "Channel name alignment" (round 190kd): Left / Center (FL's default: Center)
    static bool nameAlignLeft;
    // FL's Options › "Show a mute button on the piano roll preview" (on; round 190mk, DE-33): saved ("rackPreviewMute")
    static inline bool previewMuteButton = true;
    // FL's Options › "Zip selected" (Alt+Z) / "Unzip all" (Alt+U) (round 190mk, DE-38; core/RackZip.h): zipped
    // channels show as bars in a half-row strip; a right-click on a bar unzips that channel (FL's manual)
    void zipSelected();
    void unzipAll();
    void unzip (int row);
    bool zipStripForSnapshot (int row, juce::Rectangle<int>& strip) const; // (the strip a zipped row is in)
    int rowTopForSnapshot (int row) const { return rowY (row); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    ui::Hint hintAt (juce::Point<int> position) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // files dragged from the LYNX browser
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    // Keyboard actions (the key map's rack.* actions)
    void selectRelative (int delta, bool extend);
    void selectAll();
    bool moveSelected (int delta);
    void cloneSelected();
    void deleteSelected();
    // FL's Cut / Copy / Paste of the selected channels' steps and notes in the current pattern (Ctrl+X / C / V)
    void cutSelected();
    void copySelected();
    void pasteSelected();
    void sendSelectedToInserts();
    void toggleMute (int channel);
    void toggleSoloLock (int channel); // FL: Shift+click on the mute switch (round 190ih)
    // round 190ih (--details190ihshot): a channel's activity light at `level` (0 … 1) without a note
    void setActivityForSnapshot (int row, float level)
    {
        if (row >= (int) activityLights.size())
            activityLights.resize ((std::size_t) row + 1);
        if (row >= 0)
            activityLights[(std::size_t) row].level = level;
        repaint();
    }
    void solo (int channel);
    void setSwingMixOfSelected (int channel); // "Swing mix..." (type a value for the selected channels)

    // Workflow P2: step graph lanes, fills, rotation and organisation
    void toggleGraphLanes();
    // Piano-roll view: every row shows its notes as a miniature (a click opens them in the Piano Roll) instead of
    // the step buttons; FL's "step sequencer / piano roll overview" switch in the rack's title bar.
    void setPianoRollView (bool on);
    bool isPianoRollView() const noexcept { return pianoRollView; }
    bool areGraphLanesShown() const noexcept { return graphLanes; }
    // Round 190ce (hot-swap, core/HotSwap.h): the Browser replaces the linked channel's instrument or sample through
    // the rack's own paths (its editor closes first; a sample loads as a drop on the channel does).
    void closeEditorForHotSwap (int channel) { closeEditor (channel); }
    void assignSampleForHotSwap (int channel, const juce::File& file) { assignSample (channel, file); }
    juce::PopupMenu getChannelMenuForSnapshot (int channel) { return buildChannelMenu (channel); }
    juce::Rectangle<int> stepBoundsForTests (int row, int step) const { return stepBounds (row, step); } // (--undogesture190jv)
    // round 190in (R8): the channel's items of FL's wrapper ▸ menu (the channel and plugin windows ask through
    // channelWrapperItemsHandler)
    void fillWindowMenu (int channel, wrappermenu::Spec& spec);
    // the window a new generator opens with (round 190dx: the Playlist's new instrument tracks use it too)
    void openGeneratorWindow (int row, InstrumentKind kind) { openNewGenerator (row, kind); }
    ui::Hint getChannelHintForSnapshot (int channel); // the hint of the channel's name button (--freezetest)
    void setGraphLevelForSnapshot (int level) { graphLevel = level; graphLanes = true; updateSize(); repaint(); }
    // round 190lx (DE-31): where step `step`'s column sits at `value` in the graph lane (for synthetic mouse events)
    juce::Point<int> laneSpotForSnapshot (int step, float value) const;
    void rotateSelected (int steps);
    void fillSelected (int every);
    void showEuclideanFill (int channel);
    std::unique_ptr<juce::Component> makeEuclideanPanel (int channel); // the window's content (also for --snapshot)
    void sortChannels (ChannelSort by);
    void selectUnused();

    // FL's Channel Rack looping (round 165f-rack): the title bar's Loop button switches the current pattern's looping
    // on or off; its menu picks the loop type and burns the loops. Burn MIDI writes what a channel plays as notes.
    void toggleLooping();
    bool isLoopingOn() const;
    void showLoopMenu (juce::Component* target);
    void burnLoops (bool selectedOnly);
    void burnMidi (int channel, bool toNewPattern);

    // Adds a channel at the end (routed to a free insert), selects it and, for plugins, opens its window.
    int addChannel (ChannelDef channel, bool openEditor, int insert = -1); // insert: see addChannelRouted

    // Loads `file` into the selected sampler channel (Browser: Enter, Shift+Up/Down). Returns false when
    // the selected channel cannot take a sample.
    bool loadSampleIntoSelected (const juce::File& file);

    // Opens or closes the channel's editor (plugin window or settings window; built-in drums and audio
    // clips have none). `keepOthers`: don't close the previously opened editor.
    void toggleEditor (int channel, bool keepOthers);
    void showEditor (int channel) { if (! isEditorOpen (channel)) toggleEditor (channel, true); } // (round 190my: opens it, never closes)

    void updateSize();

    // Width that shows the controls and at most `maxSteps` steps (the panel scrolls for longer patterns).
    int widthForSteps (int maxSteps) const { return gridLeft() + juce::jmin (numSteps(), maxSteps) * stepWidth + 8; }
    int naturalWidth() const { return gridLeft() + numSteps() * stepWidth + 8; } // without the ghost steps

    // The menu of instruments and plugins to add as a new channel (also the toolbar's plugin picker).
    void showAddChannelMenu (int insertAt = -1);
    // The same items into `menu` (also the main window's Add menu, REA round 139).
    void addInstrumentItems (juce::PopupMenu& menu, int insertAt);

    // Channel filter groups (FL's "All" dropdown in the rack's title, REA round 104): -1 shows every channel,
    // otherwise only the channels of that group (Song::channelGroups). New channels join the group shown.
    int getFilterGroup() const noexcept { return filterGroup; }
    void setFilterGroup (int group);
    void stepFilterGroup (int direction); // round 190ld: FL's Page Up / Page Down
    // round 190ld (DF-118): FL's wrapper Channel column, Save channel state as... / Browse channel states
    void saveChannelState (int channel);
    void openChannelState (int channel);
    static juce::File channelStateFolder();
    juce::String filterName() const;
    void showFilterMenu (juce::Component* target);
    void rename (int channel); // "Rename and color..." (F2; issue #201: a new colour goes to every selected channel)
    void groupSelected (); // "Group selected..." (Alt+G): the selected channels go into a new, named group
    void moveSelectedToGroup (int group); // -1 = no group
    bool isShown (int row) const;          // not hidden by the filter
    std::function<void()> onFilterChanged; // the title shows the filter's name and the graph editor's state

    static constexpr int rowHeight = 30;
    static constexpr int muteWidth = 26;
    static constexpr int knobWidth = 28;
    static constexpr int routeWidth = 30;
    // FL's Options › "Channel button width" (round 190mk, DE-39; capture 190mk A2.1): 100 / 200 / 300 / 400 %, Custom (a
    // percentage), Size to fit (0 here). FL's button is 80 px at 100 % and 332 px at 400 % (+84 px per 100 %); LYNX's 150 px
    // button grows in the same proportion. Saved ("rackButtonWidth")
    static inline int nameWidth = 150;
    static inline int buttonWidthPercent = 100;
    static int widthForPercent (int percent) noexcept { return juce::roundToInt (150.0 * (1.0 + 1.05 * (juce::jlimit (100, 400, percent) / 100.0 - 1.0))); }
    void setButtonWidth (int percent); // 0 = size to fit the longest name
    static constexpr int selectWidth = 12;
    static constexpr int stepWidth = 18; // (round 190io, FM-11: FL's keycap pitch is 16, LYNX 18; was 22)

private:
    enum class Area { none, mute, pan, volume, route, name, select, grid, addRow, laneLevels, lane, loop, zipped };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    // the playing step (and the selectors' hit flashes), at the display's refresh (round 174e; was the 30 Hz timer)
    bool playheadFrame (double nowMs);
    void followPlayhead();

    Area areaAt (int x, int y, int& row) const;
    void moved() override;
    int stickyX = 0; // how far the viewport scrolled the rack sideways (the controls column stays in view)
    int numSteps() const;   // the pattern's steps
    int shownSteps() const; // plus FL's ghost steps: the free width after the pattern's end (issue #11)
    int ghostSteps = 0;
    // channel looping: the row's loop point in steps (0: the row doesn't loop), the advanced controls' column (FL's
    // LoopWP: at the right edge of the visible rack) and its loop point menu
    int loopStepsOf (int row) const;
    bool advancedLoopShown() const;
    static constexpr int loopColumnWidth = 44;
    juce::Rectangle<int> loopBoxBounds (int row) const;
    void paintLoopColumn (juce::Graphics& g);
    void showLoopPointMenu (int row);
    void setLoopPoint (int row, int setting);
    int loopDragRow = -1, loopDragStart = 0;
    float loopDragY = 0.0f;
    void parentSizeChanged() override { updateSize(); repaint(); } // (the fitted miniatures follow the visible width)
    int nameLeft() const noexcept { return muteWidth + 2 * knobWidth + (showMixerSelectors ? routeWidth : 0) + 2; }
    int selectLeft() const noexcept { return nameLeft() + nameWidth + 4; }
    int gridLeft() const noexcept { return selectLeft() + selectWidth + 8; }
    juce::Rectangle<int> rowBounds (int row) const;
    juce::Rectangle<int> previewMuteBounds (int row) const; // (round 190mk: the preview's round mute button)
    int rowY (int row) const;       // top of a row (rows below the graph lane shift down)
    // round 190mk (DE-38): the rows' places while any channel is zipped (empty: none is, FL's fixed 30 px rows)
    rackzip::Layout zipLayout;
    void refreshZipLayout();
    bool isZipped (int row) const noexcept { return ! zipLayout.slot.empty() && row >= 0 && row < (int) zipLayout.slot.size() && zipLayout.slot[(std::size_t) row] >= 0; }
    int zipBarAt (int row, int x) const; // the zipped row whose bar is under x (x in the rack's coordinates), -1 none
    juce::Rectangle<int> zipBar (int row) const; // a zipped row's bar
    void paintZipStrips (juce::Graphics& g);
    int filterGroup = -1;
    static inline ChannelStepsClipboard stepClipboard; // Cut / Copy / Paste (Ctrl+X / C / V, the channel menu)
    int visibleIndex (int row) const; // the row's place among the shown rows
    int shownCount() const;
    int rowAtY (int y) const;       // row under y (the lane counts as its channel's row)
    int graphRow() const;           // the row the lane belongs to (the primary selection), -1 when hidden
    juce::Rectangle<int> laneBounds() const;
    juce::Rectangle<int> levelButtonBounds (int tab) const; // tab = FL's lane order (notetools::stepLevel)
    void paintLane (juce::Graphics& g);
    void paintNotePreview (juce::Graphics& g, int row, const Pattern& pattern, std::uint32_t channelColour);
    PreviewSpan previewSpan() const; // where the miniatures are drawn (core/RackOps.h notePreviewSpan)
    void setLaneValue (int step, float value);
    void applyLane (const juce::MouseEvent& e);
    float laneValueAt (int y) const;
    std::pair<int, int> pitchLaneKeys() const; // the keys the Note pitch lane shows (issue #208)
    bool graphLanes = false;
    bool pianoRollView = false;
    int graphLevel = 0; // notetools::Level
    int laneDownStep = -1;
    float laneDownValue = 0.0f;
    // round 190lx (DE-31, FL manual "Scale all values simultaneously - Hold Ctrl and adjust one column, the others will
    // follow"): the lane's notes and values at a Ctrl press; the drag scales them all by the pressed column's change
    std::vector<std::pair<std::size_t, float>> laneScaleFrom;
    float laneScaleBase = -1.0f; // the pressed column's value at the press (< 0: no Ctrl scaling)
    static constexpr int laneHeight = 96; // (three rows of FL's nine lane tabs, issue #208)
    juce::Rectangle<int> stepBounds (int row, int step) const;
    bool cellHasNote (int channel, int step) const;
    std::vector<std::uint8_t> noteCellBits; // (paint: [channel * steps + step] a Piano Roll note starts there; round 190gr)
    void applyPaint (const juce::MouseEvent& e);
    juce::PopupMenu buildChannelMenu (int channel);
    juce::PopupMenu replaceMenu (int channel);
    void showChannelMenu (int channel);
    void chooseSample (int channel, int insertAt = -1); // channel < 0 = new channel (at the end, or at row insertAt)
    void openNewGenerator (int row, InstrumentKind kind); // the window a new / replaced generator opens with
    void assignSample (int channel, const juce::File& file);
    void assignSampleNow (int channel, const juce::File& file); // (no Speech properties step)
    void openPluginWindow (int channel);
    // Freeze (round 190ay, ui/ExportDialog.h freezeChannel): the frozen channel's hint; "Unfreeze to edit" in the hint
    // bar when an edit of a frozen channel is refused (true = refused); Freeze / Re-freeze of several channels, one
    // render after the other (by uid: the rack may change between two renders)
    juce::String frozenHint (int channel);
    bool refuseFrozenEdit (int channel);
    void freezeChannels (std::vector<std::uint64_t> uids);
    void sliceChannel (int channel);
    bool isEditorOpen (int channel);
    void closeEditor (int channel);

    // selection
    void selectRows (std::vector<int> rows, int primary);
    void clickSelector (int row, const juce::ModifierKeys& mods);
    int selectDownRow = -1;          // a drag over the selector column: where it started
    bool selectDragAdds = false;     // right-drag: add the crossed rows to the selection
    std::vector<int> selectDragBase; // the selection before a right-drag

    // channel button: a click opens the editor on release; dragging reorders the selection
    int nameDownRow = -1;
    bool nameDragged = false, nameDownAlt = false;
    float nameDragY = 0.0f;

    // hover feedback
    void setHover (Area area, int row, int step);
    juce::Rectangle<int> controlBounds (Area area, int row) const; // knob / route box under the mouse (value tip)
    std::uint32_t shownAutomation = 0; // AutomationDisplay generation last drawn (issue #12)
    std::vector<int> automatedRows;
    // round 190ih: each channel's activity light (FL's selector lights with every note the channel plays, then fades)
    std::vector<activity::Light> activityLights;
    void updateActivityLights();
    juce::Rectangle<int> selectorBounds (int row) const;
    std::vector<std::array<float, 2>> shownLevels; // volume, pan per channel at the last full refresh (round 173f)
    void showTip (Area area, int row, bool transient);
    Area hoverArea = Area::none;
    int hoverRow = -1, hoverStep = -1;

    // the control contract for the row's knobs and route box
    ui::ValueSpec specFor (Area area, int row) const;
    double valueOf (Area area, int row) const;
    void setValue (Area area, int row, double value);

    AudioEngine& engine;
    int playheadStep = -1;
    static constexpr int positionStripHeight = 9; // FL's "Playing step" LED strip, at the top of the add row
    bool channelHitsNow (int row) const;
    static void paintStepHit (juce::Graphics&, juce::Rectangle<float>);
    static void paintKeycap (juce::Graphics&, juce::Rectangle<int>, juce::Colour fill, bool lit); // FL's raised step (round 190io)
    static constexpr float keycapRadius = 2.0f;

    enum class PaintMode { none, add, remove } paintMode = PaintMode::none;
    int lastPaintedRow = -1, lastPaintedStep = -1;

    Area knobDrag = Area::none;
    int knobRow = -1;
    ui::ValueDrag drag;
    HeldNotes previews;        // Ctrl+click audition and step-click previews: they stop on mouse up
    int openEditorRow = -1;    // the editor opened last by a click on a channel button
    int selectionAnchor = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;
    ui::FrameTicker frames { *this, [this] (double ms) { return playheadFrame (ms); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
    std::unique_ptr<ui::TimerWhileShowing> showingTimer; // the polling timer, only while on screen (round 174f)
};

// The Channel Rack panel's viewport: a sample, SoundFont or generator dropped on the free space under the channels
// becomes a new channel too, as anywhere in FL's Channel Rack (issue #11); the drop goes to the rack.
class ChannelRackViewport final : public juce::Viewport,
                                  public juce::FileDragAndDropTarget,
                                  public juce::DragAndDropTarget
{
public:
    explicit ChannelRackViewport (ChannelRackComponent& r) : rack (r)
    {
        // juce::Viewport makes itself transparent to clicks, so drag-and-drop would skip it and the empty space under
        // the last channel would not accept drops. Children still get their own clicks.
        setInterceptsMouseClicks (true, true);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override { return rack.isInterestedInFileDrag (files); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        const auto p = rack.getLocalPoint (this, juce::Point<int> (x, y));
        rack.filesDropped (files, p.x, p.y);
    }
    bool isInterestedInDragSource (const SourceDetails& details) override { return rack.isInterestedInDragSource (details); }
    void itemDropped (const SourceDetails& details) override
    {
        auto moved = details;
        moved.localPosition = rack.getLocalPoint (this, details.localPosition);
        rack.itemDropped (moved);
    }

private:
    ChannelRackComponent& rack;
};
} // namespace lynx
