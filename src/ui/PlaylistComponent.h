#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/PickerSelection.h"
#include "engine/AudioEngine.h"
#include "ui/EditorTools.h"
#include "ui/FrameTicker.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
class PatternBlocksView;
class VideoLanesView;

// FL-style Playlist (arrangement): tracks vertically, time horizontally.
//  - a tool strip (Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen, Slip); with the Pencil:
//    left click on empty space "paints" the current source (and starts dragging it), dragging a clip moves
//    it (time and track), its edges trim it without moving the content (Shift+edge too), and the fade
//    handles of audio clips set the fades
//  - every tool: right click deletes (dragging deletes several; press and hold on empty space = Knife),
//    Ctrl+drag selects (Ctrl+click adds), Alt ignores the grid; Shift+drag on a clip drags a copy;
//    Alt+Shift+drag inside a clip slips its content
//  - clicking a clip makes its pattern the current one; double click opens it in the Channel Rack
//  - ruler: click sets the playback position (Song mode); Ctrl+drag makes a time selection (it loops and
//    selects the clips it touches); a double click clears it
//  - dragging an audio file into the Playlist creates an audio clip
//  - wheel scrolls (smoothly), Shift+wheel horizontal, Ctrl+wheel zooms at the mouse, Shift+Alt+wheel
//    nudges the selection; Ctrl+right-click on empty space zooms to everything and back
//  - the clip under the mouse is outlined, its edges show grips and its title strip a menu button; the
//    cursor and the hint bar tell what a click would do (core/EditorGestures.h)
//  - right click on a clip's name strip (or its menu button) opens its menu (split, make unique, etc.)
//  - track header (FL's layout: name, group arrow, LED on the right): click selects the track, the LED mutes it
//    (Ctrl+click or right-click on it: solo), the arrow of a group header folds / unfolds its group; an audio
//    track's record button arms / disarms its mixer track and its input button picks the input (FL 2026); right
//    click elsewhere opens the track menu (FL's order); double click opens the source of the track's first
//    clip; Shift+wheel moves the track up / down. Grouped tracks are indented under their header (REA round 104)
//  - keys (via handleKey): Delete, Backspace (global grid on/off), Ctrl+A/C/X/V, Ctrl+B (repeats the
//    time selection when there is one), Ctrl+Ins / Ctrl+Del insert / remove the time selection, Esc
class PlaylistComponent final : public juce::Component,
                                public SharesTitleBar,
                                public juce::FileDragAndDropTarget,
                                public juce::DragAndDropTarget,
                                public ui::HintSource,
                                private juce::ChangeListener,
                                private juce::ScrollBar::Listener
{
public:
    // What the pencil paints (chosen in the Picker): the current pattern, an audio clip or an automation clip.
    struct Brush
    {
        ClipKind kind = ClipKind::pattern;
        int index = -1; // audio or automation channel; for patterns the current pattern is used
    };
    Brush brush;
    int knownCurrentPattern = -1;

    explicit PlaylistComponent (AudioEngine& engine);
    juce::String cacheSelfTest (const std::function<void()>& waitForWaveforms, const juce::File& dumpFolder = {}); // --cachetest (round 174f)
    ~PlaylistComponent() override;
    void visibilityChanged() override;

    std::function<void()> onOpenPattern;
    std::function<void()> onOpenPianoRoll;
    std::function<void (int pattern)> onRenamePattern;  // clip menu: Rename and color (pattern clips)
    std::function<void (int channel)> onOpenInSampler;  // clip menu: the audio clip's file in a new Sampler channel

    // Double-click on a pattern clip opens it in: 0 = the Piano Roll when it has piano-roll notes, else the
    // Channel Rack; 1 = always the Channel Rack; 2 = always the Piano Roll. Alt+double-click opens the other one.
    static inline int editPatternsIn = 0;
    std::function<void()> onViewOptionChanged; // an app-wide view option changed (round 177b: Automation lanes): save it

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
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

    // Editing shortcuts; the main window forwards keys when the Playlist is visible.
    bool handleKey (const juce::KeyPress& key);
    // The key map's actions of this editor (its tools and single letters: "pianoRoll.tool.knife"...); false if not one.
    bool runKeyAction (const std::string& id);
    // The tool buttons' tooltips show the keys the key map gives them (`keyOf`: action id -> key text, "" = none).
    void refreshToolKeys (const std::function<juce::String (const std::string&)>& keyOf);

    // The key map's playlist.* actions (zoom presets, ripple editing). False for unknown ids.
    bool runAction (const std::string& id);
    // FL's "Clone — What do you want to clone along with the track?" window (round 168f), for --snapshot too.
    static std::unique_ptr<juce::AlertWindow> makeCloneTracksDialog (bool groupHeaderSelected);

    // Ripple editing: resizing a clip's end moves the later clips on its track, deleting closes the gap.
    bool isRippleOn() const noexcept { return ripple; }
    bool clipProgressShown() const noexcept { return showClipProgress; }
    bool trackProgressShown() const noexcept { return showTrackProgress; }
    void setRipple (bool on);
    void rippleDeleteSelection();

    // dragging audio files creates audio clips (and a channel for each)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // files dragged from the LYNX browser
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    // The Browser's "Send to the Playlist as an audio clip": at the playhead (song mode) or the start, on the
    // first track that is free there; an audio-clip channel of the same file is reused.
    void addAudioClipAtPlayhead (const juce::File& file);

    // For snapshots: the tool in use and a time selection over [fromBar, toBar).
    void setToolForSnapshot (EditTool tool) { tools.setTool (tool); }
    void setTimeSelectionForSnapshot (int fromBar, int toBar);
    // the selected clips (indices into the Playlist), for Tools › Consolidate playlist selection (round 168e)
    const std::vector<int>& getSelectedClips() const noexcept { return selection; }
    // zoom (pixels per bar) and scroll (first bar, first track in view)
    void setViewForSnapshot (double pixelsPerBar, int firstBar, int firstTrack);
    juce::PopupMenu getClipMenuForSnapshot (int index); // --snapshot: the clip menu of that clip
    juce::PopupMenu getTrackMenuForSnapshot (int track) { return buildTrackMenu (track); }
    // hovers the right edge of the first clip in view (grip, outline, menu button); returns its hint
    ui::Hint hoverForSnapshot();

    static constexpr int headerWidth = 96;
    static constexpr int toolbarHeight = 32; // the panel title row (Workspace::titleHeight)
    static constexpr int overviewHeight = 16; // the overview strip under the toolbar (round 90)
    static constexpr int timelineHeight = 24;
    static constexpr int rulerTop = toolbarHeight + overviewHeight;
    static constexpr int headerHeight = rulerTop + timelineHeight;
    static constexpr int trackHeight = 38; // the default row height (each track can be resized: P18)
    static constexpr int foldedHeight = 7; // a member of a collapsed group: a thin row of clip bars (as FL)
    static constexpr int groupIndent = 8;  // grouped tracks' headers are indented under their group header
    static constexpr int scrollBarSize = 12;

    static juce::Colour patternColour (const Song& song, int pattern);

    // FL's legacy Pattern Blocks (round 170f, issue #60): the section under the clip tracks while the project's option is
    // on (ui/PatternBlocksView.h). `height` = the section's height in px (the divider drag); clamped to the window.
    void setBlocksHeight (int height);
    int blocksSectionHeight() const;
    void togglePatternBlocks();       // the option, asking what to do with existing blocks when it goes off
    void turnBlocksIntoClips();       // FL's "Turn pattern instances into clips"
    void scrollTicksBy (double ticks); // (the blocks section's drag past its edges)
    PatternBlocksView* getBlocksViewForSnapshot() { return blocksView.get(); }
    // Video tracks (round 174d, ui/VideoTracks.h): the section between the ruler and the clip tracks while the project
    // has video tracks. `height` = the section's height (the divider drag; 0 = fit the lanes), clamped to the window.
    void setVideoHeight (int height);
    int videoSectionHeight() const;
    int tracksTop() const { return headerHeight + videoSectionHeight(); } // the clip tracks' top edge
    VideoLanesView* getVideoLanesForSnapshot() { return videoLanes.get(); }
    static juce::Colour audioClipColour (const ChannelDef& channel);

    // A project was just opened: scroll to the start and, when the song is longer than the view, zoom out
    // to show all of it (FL opens a project with the arrangement in view). Waits for a size if not laid out.
    void showSongAfterOpen();

private:
    enum class Drag { none, move, resize, trimLeft, slip, erase, seek, loop, select, fadeIn, fadeOut, paint, muting, knife, zoomRange, listen, pan, timeMove, timeShift, autoPoint, autoTension,
                      lanePoint, laneTension, trackResize, preview, trackZoom, heightZoom, timeZoom, perfLaunch, clipGain };
    // Performance mode (round 165m): the clip a click launched (its release goes with the mouse button)
    int perfTrack = -1;
    std::int64_t perfClip = -1;
    std::array<std::int64_t, 64> perfShown {}; // the zone's playing / pending state last painted, to repaint on change
    void paintPerformance (juce::Graphics& g, juce::Rectangle<int> grid);
    void paintLiveRecording (juce::Graphics& g); // round 177d
    juce::Rectangle<float> liveBounds (int track, double from, double to) const;
    juce::Rectangle<int> liveArea;
    double liveRepaintMs = 0.0;
    std::vector<float> liveColumns;
    void togglePerformanceMode();
    void zoomOnPerformanceClips();
    // round 168f: FL's Playlist track tools (core/PlaylistTrackOps.h)
    void mergePatternClipsByKey();               // Ctrl+G
    void makeSelectionUnique();                  // Ctrl+Shift+V
    void cloneTracksWithDialog (std::vector<int> tracks); // Ctrl+Shift+B, track menu "Clone..."
    void lockAllTracks (bool lock);              // Tools › Lock / Unlock all playlist tracks (to / from) content
    void resetEmptyTracksAsked();                // Tools › Reset empty playlist tracks
    void setStretchOnResize (bool on);           // the STRETCH switch (Shift+M)
    std::vector<Clip> stretchOriginals;          // the clips when a stretching resize started
    IconButton stretchButton { "stretch", Icon::stretch };
    bool showClipProgress = true, showTrackProgress = true; // FL's View › Performance clip / track progress

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    // the playhead and Performance mode's live clips, at the display's refresh (round 174e; was a 30 Hz timer)
    bool liveFrame (double nowMs);
    double perfRepaintMs = 0.0;
    // The view kept as an image, the playhead drawn over it (round 174e): a playhead frame only blits its column of the
    // image and draws the line, instead of painting the lanes, clips and waveforms under it again 144 times a second.
    // Any other repaint (an edit, a scroll, a child) refreshes its area of the image. LYNX_PLAYLIST_CACHE=0: off.
    class PlayheadCache;
    bool playheadOverlay = false;      // the cache is on: the playhead's own repaints keep its image
    bool playheadRepaintOnly = false;  // set around the playhead's own repaints: the image stays valid
    bool cachePainting = false;        // paint() is filling the image: no playhead in it
    bool scrollRepaintOnly = false;    // set around a scroll's repaint: the cache shifts its picture (round 174e2)
    struct CacheView
    {
        double scrollTicks = -1.0;
        int scrollY = -1;
        double pixelsPerTick = 0.0;
        int tracksHeight = 0, tracks = 0;
        bool overlays = false; // the Ripple / Performance tags in the grid's corner (they do not scroll)
        int width = 0, height = 0;
        bool operator== (const CacheView&) const = default;
    };
    struct CacheShift
    {
        juce::Rectangle<int> area, alsoChanged;
        int dx = 0, dy = 0;
    };
    CacheView cacheView() const;
    CacheShift cacheShift (const CacheView& from, const CacheView& to, float scale) const;
    void repaintScrolled();
    // a continuous zoom (round 174f): its frames scale the cached picture's lanes; exact once still for zoomSettleMs
    void repaintZoomed();
    void settleZoom();
    bool zoomRecent (double nowMs) const;
    juce::Rectangle<int> rulerArea() const;
    static constexpr int zoomSettleMs = 120;
    bool zoomGesture = false;
    double zoomEndMs = -1.0e9;
    juce::TimedCallback zoomSettle { [this] { settleZoom(); } };
    void paintPlayhead (juce::Graphics& g);
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int trackToY (int track) const;
    int yToTrack (int y) const;
    int numTracks() const;
    // track rows have their own heights (Song::playlistTracks[i].height): tops are cached here
    std::vector<int> trackTops;          // top of each track from the first one, plus the total at the end
    void refreshTrackLayout();
    int heightOf (int track) const;
    // FL's vertical zoom (issue #11): scales every track's height; Alt+wheel, or drag the handle at the top of the
    // vertical scroll bar (up = taller), double-click it: back to 100%
    double trackZoom = 1.0;
    int scaledHeight (int storedHeight) const { return juce::jlimit (8, 640, (int) std::lround (storedHeight * trackZoom)); }
    int defaultRowHeight() const { return scaledHeight (trackHeight); }
    juce::Rectangle<int> trackZoomHandle() const;
    void setTrackZoom (double zoom, int anchorY);
    double trackZoomStart = 1.0;
    // FL 2026 middle-drag zooms (round 167t, issue #26): Ctrl+middle-drag on the clips = track height around the
    // track under the mouse; middle-drag on the ruler / Alt+middle-drag on the clips = time zoom
    gesture::HeightZoomDrag heightZoomDrag;
    gesture::SpanZoomDrag timeZoomDrag;
    int heightZoomTrackY = 0, heightZoomStartHeight = 1;
    bool trackZoomHover = false;
    int tracksHeight() const;            // all tracks, top to bottom
    int dividerAt (juce::Point<int> p) const; // the track whose bottom edge (in the headers) is under p, or -1
    void setTrackHeights (std::vector<int> tracks, int height);
    int resizeTrack = -1, resizeStartHeight = 0;
    bool resizeAll = false;
    // Alt+right-click on a clip: hear it while the button is held (pattern clips play their pattern)
    void startPreview (int clip, int x);
    void stopPreview();
    bool previewRestoreSong = false;
    int previewPatternBefore = -1;
    double previewPositionBefore = 0.0;
    std::int64_t gridTicks() const; // the grid step in use (resolving Screen / Nearest note)
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t snapLine (double tick) const; // the nearest grid line (the ruler, round 169d)
    std::vector<std::int64_t> snapCandidates() const;
    int clipAt (int x, int y, bool& onRightEdge) const;
    int clipAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const;
    Clip makeBrushClip (int track, std::int64_t start) const; // what a click on empty space places
    void cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn);
    void zoomToTicks (double from, double to);
    void zoomIn (int anchorX);
    void zoomOut (int anchorX);
    void setTimeSelection (std::int64_t from, std::int64_t to); // also selects the clips it touches
    void insertTimeSelection();
    void deleteTimeSelection();
    // fade handle of an audio clip under the mouse: 1 = fade-in, 2 = fade-out, 0 = none
    int fadeHandleAt (int clip, juce::Point<int> p) const;
    void updateScrollBars();
    bool isSelected (int index) const;
    void setSelection (std::vector<int> indices);
    // sorts while keeping the selection (no undo step); `placed` = the clips the edit put somewhere (moved, painted,
    // pasted…): their audio clips follow an audio track (issue #15, trackmodes::followAudioTracks)
    void sortClips (std::vector<int> placed = {});
    void askToMakeClipsUnique (std::vector<int> placed, std::vector<int> channels);
    void showClipMenu (int clip);
    void showRulerMenu (int x);                           // markers, time signature and loop
    static juce::String markerLabel (const Marker& m);   // the text of a marker's tag on the ruler
    int markerTagAt (int x, int y) const;                 // the marker whose tag is under (x, y), or -1
    void editMarkerName (std::int64_t tick, int existing); // existing = index, or -1 to create
    // FL 2026's Playlist › Time markers keys (round 167k): Alt+T, Ctrl+T, Shift+Alt+T, Shift+T
    std::int64_t songPositionTick() const;      // the playhead (song position), wrapped like centreOnPlayhead
    void addMarkerByKey();                      // "Add one..." at the time selection / visible playhead / first bar
    void addAutoMarker();                       // "Add auto" at the playhead, on the grid
    void addSignatureChangeByKey();             // "Add time signature change..." (dialog)
    void placeLoopMarker();                     // "Place loop" at the time selection / first bar in view
    // FL's Select › time keys (round 167k: EESelAroundSelAction Ctrl+Enter, EESelPrev/NextAction Ctrl+Left/Right)
    void selectTimeAroundSelection();
    void moveTimeSelection (int direction);
    void deleteSelection();
    void copySelection();
    void pasteClips (std::int64_t at = -1);
    void normalizeClips (const std::vector<int>& targets);
    juce::PopupMenu buildClipMenu (int index, int mouseX);
    void duplicateSelection();
    void seekTo (int x, bool snapOn = true); // Alt held while scrubbing: snapOn false (free position, FL)
    void paintAudioClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    // Clips a few pixels wide (zoomed far out), gathered and filled per colour at the end (round 151)
    struct SmallClips;
    void addSmallClip (SmallClips& batch, float physicalScale, const Clip& clip, juce::Rectangle<float> bounds, bool current);
    void paintAutomationClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);

    void refreshArrangementBox();
    void arrangementChosen();
    void renameArrangement();
    void renameTrack (int track);
    void showTrackMenu (int track);
    juce::PopupMenu buildTrackMenu (int track);
    void openClipSource (int clip, bool other); // double click: the clip's pattern / automation (Alt: the other editor)
    juce::Rectangle<int> trackLed (int track) const;   // the mute LED on the right of a header
    juce::Rectangle<int> trackArrow (int track) const; // a group header's fold arrow (empty for other tracks)
    // An audio track's buttons (FL 2026, issue #11), acting on its linked mixer track: "Arm/disarm recording" (left of
    // the LED / arrow) and, on rows tall enough, "Audio input source" (with the monitoring choices). Empty for others.
    juce::Rectangle<int> trackArm (int track) const;
    juce::Rectangle<int> trackInput (int track) const;
    static constexpr int inputButtonMinHeight = 52; // FL shows the row of buttons from 150 % of its track height
    void editTrack (int track, std::function<void (PlaylistTrack&)> change);
    int currentTrack = -1; // the track selected by a click on its header (FL marks it next to the LED)
    std::vector<int> selectedTracks; // Ctrl / Shift+click on headers: more selected tracks (round 165m)
    bool isTrackSelected (int track) const;
    std::vector<int> tracksForMenu (int track) const;

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true }; // hBar is hidden: the overview strip scrolls sideways
    OverviewBar overview;
    void paintOverview (juce::Graphics& g, juce::Rectangle<float> area, double total);
    std::size_t overviewSignature = 0; // the clips drawn in the overview (repaints it when they change)
    IconComboBox arrangementBox { Icon::song }; // round 170e2: the arrangement icon names the box
    SnapBox snapBox { true }; // "Main" (the toolbar snap) or this editor's own grid
    ToolStrip tools { true };
    IconButton followButton { "follow", Icon::follow }; // Follow (auto scrolling) on/off, EditorGlobals (round 162)
    void quantizeClips (double strength); // the selected clips' starts (all clips without a selection)
    // FL 2026's keys 7 / 8 / 9 (round 167k): the selected audio clips' own pitch one semitone down / up, reverse
    void pitchSelectedAudioClips (int semitones);
    void reverseSelectedAudioClips();
    int titleInsetLeft = 0, titleInsetRight = 0; // the window's title and buttons share the toolbar row

    double pixelsPerTick = 96.0 / (4 * ppq); // 96 px per 4-beat bar
    double scrollTicks = 0.0;
    int scrollY = 0;
    bool fitSongPending = false;

    Drag drag = Drag::none;
    int dragClip = -1;
    std::vector<int> dragIndices;
    std::vector<Clip> dragOriginals;
    std::vector<int> selection, selectionBefore;
    juce::Rectangle<int> rubberBand;
    std::int64_t loopAnchor = 0;
    juce::ModifierKeys downMods;
    bool startedOnEmpty = false, erasedSomething = false, holdExpired = false;
    std::vector<int> touchedClips;
    std::int64_t nextPaintTick = 0;
    Clip paintClip;
    juce::Point<int> knifeFrom, knifeTo;
    std::size_t knownClipCount = 0;
    static inline std::vector<Clip> clipboard;
    std::int64_t grabOffset = 0;
    double playheadTick = -1.0;

    // workflow P2
    bool ripple = false;
    std::vector<Clip> rippleOriginals;   // the whole playlist when a ripple resize starts
    double panStartTicks = 0.0;
    int panStartY = 0;
    std::vector<Clip> timeMoveOriginals; // Shift+drag on the ruler: the playlist when it started
    std::int64_t timeMoveFrom = 0, timeMoveTo = 0, timeMoveAnchor = 0;
    bool timeMoveCopy = false;
    juce::uint32 lastRightClickTime = 0; // FL: a double right-click (held) is the Mute tool, on clips or empty space
    juce::Point<int> lastRightClickPos;
    gesture::MuteSweep muteSweep;        // the mute drag takes its target (mute / unmute) from the first clip crossed
    // inline automation editing: points of automation clips are dragged, added on the curve, deleted by right-click
    int autoClip = -1, autoPoint = -1;
    // FL 2026's audio clip gain handle (round 166e): a semicircle at the bottom centre of the clip; a vertical drag
    // changes the gain of the clip (or of every selected audio clip, each from its own value) by 0.05 dB per pixel
    juce::Rectangle<float> gainHandle (int clip) const; // empty: the clip has none (not audio, too small)
    void showGainMenu (int clip);
    std::vector<double> gainStartDb; // per dragIndices, when the gain drag started
    int gainStartY = 0;
    static inline double copiedGainDb = 0.0;
    static inline bool hasCopiedGain = false;
    // FL: a double click held on the ruler drags a time selection (as Ctrl+drag); released without moving it clears
    bool rulerDoubleDrag = false;
    bool rulerMenuOnClick = false; // a right press on the ruler: the ruler menu if released in place (round 168t)
    juce::Rectangle<float> automationBody (const Clip& clip) const;
    int automationPointAt (int clip, juce::Point<int> p) const;
    bool isOnAutomationCurve (int clip, juce::Point<int> p) const;
    // round 165: FL's in-Playlist automation editing — clips tall and wide enough to edit, the tension handles between
    // points (hollow circles; index = the point on the left, whose tension shapes the segment), the point menu
    bool automationEditable (int clip) const;
    juce::Point<float> automationHandlePos (int clip, int point) const; // x < 0: no handle
    int automationHandleAt (int clip, juce::Point<int> p) const;
    void showAutomationPointMenu (int clip, int point);
    // round 177b: optional automation lanes under a track (Playlist › Automation lanes; core autolanes). A track's
    // lanes sit below its row (heightOf stays the row: clips never draw into lanes); trackTops include them.
    int laneHeightOf (const AutomationLane& lane) const;
    int laneBlockHeight (int track) const;                 // 0: none shown
    struct LaneHit { int track = -1, lane = -1; };
    LaneHit laneAt (juce::Point<int> p) const;             // a lane row under p (header or grid)
    juce::Rectangle<int> laneRow (int track, int lane) const; // full width, from x = 0
    juce::Rectangle<float> laneBody (int track, int lane) const; // the curve's area in the grid
    int laneAutomation (int track, int lane) const;        // -1: none
    int lanePointAt (int track, int lane, juce::Point<int> p) const;
    juce::Rectangle<int> trackLaneArrow (int track) const; // empty: the track has no lanes or lanes are off
    juce::Rectangle<int> trackAddLane (int track) const;   // "+A": empty when lanes are off or the row is too small
    void paintLanes (juce::Graphics& g, juce::Rectangle<int> grid);
    void paintLaneHeaders (juce::Graphics& g, juce::Rectangle<int> headers);
    bool laneMouseDown (const juce::MouseEvent& e);        // true: the press was on a lane (handled)
    void showLaneMenu (int track, int lane);
    void showAddLaneMenu (int track);
    void toggleAutomationLanes();
    bool laneButtonHover = false;
    int laneTrack = -1, laneIndex = -1;                    // the lane a lanePoint / laneTension drag edits
    float tensionStart = 0.0f, tensionSign = 1.0f;
    // hover: what a click would hit (drives the cursor, the highlight and the hint)
    struct Hover
    {
        int clip = -1, track = -1;
        gesture::Target target = gesture::Target::empty;
        bool header = false, led = false, arrow = false, arm = false, input = false;
        int divider = -1; // the track whose bottom edge in the headers is under the mouse (resizes it)
        bool operator== (const Hover& o) const
        {
            return clip == o.clip && track == o.track && target == o.target && header == o.header && led == o.led && arrow == o.arrow
                   && arm == o.arm && input == o.input && divider == o.divider;
        }
    };
    Hover hover;
    juce::Point<int> lastMouse { -1, -1 };
    Hover hoverAt (juce::Point<int> p) const;
    void updateHover (juce::Point<int> p, juce::ModifierKeys mods);
    void updateCursor (juce::ModifierKeys mods);
    juce::Rectangle<int> clipBounds (int clip) const;
    void postDragHint (); // what the gesture in progress does (hint bar)
    juce::Rectangle<int> clipMenuButton (int clip) const; // the menu button in a clip's title strip (empty if too narrow)
    juce::uint32 lastWheelEdit = 0;
    double zoomBeforeAll = 0.0, scrollBeforeAll = 0.0; // Ctrl+right-click: zoom to everything and back

    // smooth wheel scrolling
    gesture::Glide glideX, glideY;
    bool gliding = false;
    // `immediate`: a trackpad's smooth scroll moves the view at once (the fingers carry it; round 170e)
    void glideBy (double ticks, double pixels, bool immediate = false);
    void glideFrame();
    juce::VBlankAttachment vblank { this, [this] { glideFrame(); } };
    ui::FrameTicker frames { *this, [this] (double ms) { return liveFrame (ms); } };

    void zoomAroundCentre (double factor);
    void zoomToBars (int bars);
    void zoomToSelection();
    void zoomAll();
    void centreOnPlayhead();

    // Pattern Blocks (round 170f)
    friend class PatternBlocksView;
    std::unique_ptr<PatternBlocksView> blocksView;
    int blocksHeight = 0; // 0 = the default (a third of the grid)
    bool blocksShown = false;
    // Video tracks (round 174d)
    friend class VideoLanesView;
    std::unique_ptr<VideoLanesView> videoLanes;
    int videoHeight = 0; // 0 = fit the lanes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};
// Playlist "Picker" (as in FL): three tabs at the top (patterns, audio clips, automation clips) and the list
// of that kind; clicking chooses what the Playlist pencil paints, double click opens a pattern; right click on
// a pattern opens the pattern menu; "+" at the bottom of the pattern list adds a pattern (REA round 104).
class PatternPicker final : public juce::Component,
                            public ui::HintSource,
                            private juce::ChangeListener
{
public:
    PatternPicker (AudioEngine& engine, PlaylistComponent& playlist);
    ~PatternPicker() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    // The patterns picked in the list (FL's picker selection: click, Ctrl+click, Shift+click; round 169k), in the
    // order they were picked; empty when none is picked.
    std::vector<int> pickedPatterns() const;

    std::function<void()> onOpenPattern;
    std::function<void()> onPatternMenu; // right click on a pattern (it is the current one by then)
    std::function<void()> onNewPattern;  // the "+" under the pattern list
    static constexpr int width = 150;

    // the kind listed: 0 = patterns, 1 = audio clips, 2 = automation clips
    void setView (int newView);
    int getView() const noexcept { return view; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Row
    {
        ClipKind kind;
        int index;            // pattern, audio or automation channel
        juce::String name;
        juce::Colour colour;
    };
    std::vector<Row> rows() const;
    int rowAt (int y) const;
    juce::Rectangle<int> tabBounds (int tab) const;
    juce::Rectangle<int> addBounds() const; // the "+" (pattern view only)
    int listBottom() const;

    AudioEngine& engine;
    PlaylistComponent& playlist;
    int view = 0;
    ClipKind seenBrush = ClipKind::pattern; // the list follows the pencil's kind when it changes elsewhere
    int scroll = 0; // in rows
    pickersel::Selection picked; // pattern indices (pattern view)
    bool deselectOnUp = false;   // a Ctrl+click on a picked pattern takes it out on release (FL)
    int pressedRow = -1;
    int hoverTab = -1, hoverRow = -1;
    bool hoverAdd = false;
    static constexpr int rowHeight = 28, headerHeight = 26, footerHeight = 22;
};

// The Playlist tab: the Picker on the left and the Playlist (does not own them).
class PlaylistArea final : public juce::Component,
                           public SharesTitleBar
{
public:
    PlaylistArea (PatternPicker& p, PlaylistComponent& l) : picker (p), playlist (l)
    {
        addAndMakeVisible (picker);
        addAndMakeVisible (playlist);
    }

    // the toolbar row runs across the whole window (as the title bar); the picker starts under it
    void setTitleBarInsets (int left, int right) override
    {
        titleLeft = left;
        playlist.setTitleBarInsets (juce::jmax (0, left - PatternPicker::width), right);
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto column = area.removeFromLeft (PatternPicker::width);
        picker.setBounds (titleLeft > 0 ? column.withTrimmedTop (PlaylistComponent::toolbarHeight) : column);
        playlist.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg3); // the toolbar band above the picker
        g.fillRect (0, 0, PatternPicker::width, PlaylistComponent::toolbarHeight);
        g.setColour (theme::colour::border);
        g.fillRect (0, PlaylistComponent::toolbarHeight - 1, PatternPicker::width, 1);
    }

private:
    PatternPicker& picker;
    PlaylistComponent& playlist;
    int titleLeft = 0;
};
} // namespace lynx
