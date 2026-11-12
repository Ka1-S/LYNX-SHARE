#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ArrangeOps.h"
#include "core/MagicLasso.h"
#include "core/MeterBallistics.h"
#include "core/PickerSelection.h"
#include "core/PlaylistOps.h"
#include "engine/AudioEngine.h"
#include "ui/EditorTools.h"
#include "ui/FrameTicker.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
struct SampleData; // (core/Instruments.h)
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
    // round 190dx: a new instrument track's channel was made (or its generator replaced): the main window opens it as the
    // Channel Rack opens a new generator
    std::function<void (int channel, InstrumentKind kind)> onGeneratorAdded;
    // FL's "+" at the top left of the track headers (AddInstrBtn, round 190dx): left click = this menu ("Audio track":
    // Add audio track; "Instrument track": More plugins..., the generators), right click = addAudioTrackNow
    juce::PopupMenu getAddTrackMenuForSnapshot() { return buildAddTrackMenu(); }
    juce::Component* getAddTrackButtonForSnapshot() const noexcept { return addTrackButton.get(); }
    int addAudioTrackNow(); // FL's Add audio track: the new track (selected, in view), −1 when none could be made

    // Double-click on a pattern clip opens it in: 0 = the Piano Roll when it has piano-roll notes, else the
    // Channel Rack; 1 = always the Channel Rack; 2 = always the Piano Roll. Alt+double-click opens the other one.
    static inline int editPatternsIn = 0;
    // FL 2026's Playlist menu › View › Track headers (round 190w; FL's defaults): the record / input buttons on audio
    // tracks, and a stereo level meter beside the header of audio tracks (their mixer track) and instrument tracks
    // (their channel's mixer track). Kept with the UI settings.
    static inline bool showAudioTrackControls = true, showAudioTrackLevels = true, showInstrumentTrackLevels = false;
    void headerOptionsChanged(); // repaints and saves
    std::function<void()> onViewOptionChanged; // an app-wide view option changed (round 177b: Automation lanes): save it

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void setTitleBarInsets (int left, int right) override;
    void setToolbarFitsNarrow (bool on)
    {
        if (on != toolbarFitsNarrow)
        {
            toolbarFitsNarrow = on;
            resized();
        }
    }
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
    // FL's Playlist menu › Edit and › Select submenus (round 189e: the items of FL's shared EEMenu the Playlist enables,
    // in FL's order, FL 2026 shots f04 / f06), for the Playlist's options menu and its right-click menu.
    juce::PopupMenu editMenu();
    // FL's fade handle menu for an audio clip's fade in (side 0) / out (1) (round 189i; shown by a right-click on the handle)
    juce::PopupMenu fadeHandleMenu (int index, int side);
    // FL's gain handle menu of clip `index` (round 166e; Normalize items round 189n): what showGainMenu opens; public for
    // --snapshot, which also sets the selection it reads (selectForSnapshot)
    juce::PopupMenu gainMenu (int index);
    void selectForSnapshot (std::vector<int> indices) { setSelection (std::move (indices)); }
    juce::PopupMenu selectMenu();
    // The shortcut text of a key map action for menus ("\tShift+C"; empty when unbound), set by the main window.
    static inline std::function<juce::String (const std::string& id)> shortcutText;
    // FL's "Clone — What do you want to clone along with the track?" window (round 168f), for --snapshot too.
    static std::unique_ptr<juce::AlertWindow> makeCloneTracksDialog (bool groupHeaderSelected);
    // FL's "Dropped sample(s)" question of a header drop (round 189e), for --snapshot too.
    static std::unique_ptr<juce::AlertWindow> makeHeaderDropDialog();

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
    EditTool currentTool() const noexcept { return tools.getTool(); }
    void setTimeSelectionForSnapshot (int fromBar, int toBar);
    // the selected clips (indices into the Playlist), for Tools › Consolidate playlist selection (round 168e)
    const std::vector<int>& getSelectedClips() const noexcept { return selection; }
    // zoom (pixels per bar) and scroll (first bar, first track in view)
    void setViewForSnapshot (double pixelsPerBar, int firstBar, int firstTrack);
    void selectClipsForSnapshot (const std::vector<int>& clips) { setSelection (clips); repaint(); }
    void setScrollTickForSnapshot (double tick) { scrollTicks = tick; repaint(); }
    juce::PopupMenu getClipMenuForSnapshot (int index); // --snapshot: the clip menu of that clip
    juce::PopupMenu getAutomationPointMenuForSnapshot (int clip, int point); // FL's point menu of an automation clip's point (round 190bw)
    juce::PopupMenu getTrackMenuForSnapshot (int track) { return buildTrackMenu (track); }
    // hovers the right edge of the first clip in view (grip, outline, menu button); returns its hint
    ui::Hint hoverForSnapshot();

    // The track headers' width: FL's 96 px, or the Simple workflow's wider headers (round 184a). setSimpleHeaders sets
    // it; everything else only reads it.
    static constexpr int flHeaderWidth = 96, simpleHeaderWidth = 232;
    int headerWidth = flHeaderWidth;
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

    // The Simple workflow's track headers (round 184a, slice S4; core/SimpleTracks.h): wider headers whose controls act
    // on the track's mixer insert (mute, solo, record arm, volume, pan, "+FX"), taller rows by default, a lane under
    // the tracks to drop files on, and a click that also selects the track's channel. Off: FL's headers, unchanged.
    void setSimpleHeaders (bool on);
    // The Clip-launch workflow (round 190cg): track fold, "Group with above track" makes the group a bus, and automation
    // follows clips unless set otherwise. The data is the Song's either way; the other layouts show it all the same.
    void setLiveStyle (bool on);
    bool hasLiveStyle() const noexcept { return liveStyle; }
    static constexpr int liveFoldedHeight = 17; // a folded track: its title bar's height
    bool automationFollows() const noexcept;
    // 0 duplicate, 1 cut, 2 copy, 3 paste (at the time selection's start, else the song position), 4 crop the selected clips
    void timeCommand (int which);
    void setLoopSwitch (bool plays);
    float laneShown (int automation, float value) const;   // a lane value as a fraction of the lane's height
    float laneValueOf (int automation, float shown) const; // and back
    void askTempoRange();
    bool hasSimpleHeaders() const noexcept { return simpleHeaders; }
    std::function<void (int insert)> onTrackEffects; // "+FX" on a Simple header: the insert's effects
    std::function<void (int track)> onSimpleTrackSelected; // the Simple workflow: a track was selected (its dock follows)
    // Files dropped below the tracks (or MIDI files anywhere) while the Simple headers are on: new tracks from `tick`
    std::function<void (const juce::StringArray& files, std::int64_t tick)> onSimpleFilesDropped;

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

    // The track selected by a header click (FL's mark next to the LED), −1 = none; selectTrack selects one track as a
    // click on its header does, without the menus, and scrolls it into view (round 184a, S4: a new Simple track; S16:
    // the game controller's D-pad).
    int getCurrentTrack() const noexcept { return currentTrack; }
    void selectTrack (int track);

private:
    enum class Drag { none, move, resize, trimLeft, slip, erase, seek, loop, select, fadeIn, fadeOut, paint, muting, knife, zoomRange, listen, pan, timeMove, timeShift, autoPoint, autoTension,
                      lanePoint, laneTension, trackResize, preview, trackZoom, heightZoom, timeZoom, perfLaunch, clipGain, swipe, warpMarker };
    void swipeTimeSelectionToComp (int lane);
    // a swipe on a take lane (round 177c): the lane and the range so far
    int swipeLane = -1;
    std::int64_t swipeAnchor = 0, swipeEnd = 0;
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
    void mergeSimilarClipsByKey();               // Ctrl+Shift+G
    void mergeAutomationClipsByKey();            // Ctrl+Alt+G (Ctrl+G on a selection of automation clips too)
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
    // FL's audio clip menu › Preview (EEAUPreviewMenuClick, round 190dx): the clip's own audio, alone, once
    void previewClipAudio (int clip);
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
    void sliceInsertTimeSelection(); // round 189e: FL's Slice & insert space / Trim selection
    void trimTimeSelection();
    void shiftSelectedClips (int steps, int tracks, bool fine = false); // FL's Shift left / right / up / down; fine = FL's Alt step
    void moveClips (std::vector<int> chosen, std::int64_t delta, int tracks, bool newUndoStep, bool keepInView); // (keys and wheel nudge)
    void selectAllWithTime(); // FL's Select all (Ctrl+A): every clip and the whole song's time (round 189g)
    void makeSelectionUniqueAsSample(); // FL's Make unique as sample (round 189g)
    void showFadeHandleMenu (int index, int side); // FL's fade handle menu (round 189i)
    void selectBySelectedSource();
    // FL's header drop (round 189e, trackmodes::dropOnHeader): the files on header `track`, asking first when the mode is
    // "Always ask"
    void dropOnTrackHeader (const juce::StringArray& files, int track);
    void dropOnTrackHeaderAs (const juce::StringArray& files, int track, trackmodes::HeaderDrop how);
    // fade handle of an audio clip under the mouse: 1 = fade-in, 2 = fade-out, 0 = none
    int fadeHandleAt (int clip, juce::Point<int> p) const;
    // warp markers (round 175a, M3; a LYNX extra, the user's choice: Live edits them only in its Clip View): an audio
    // clip whose channel has markers that bend the tempo shows them as small flags on its top edge, to drag sideways
    std::vector<WarpPoint> clipWarpMarkers (int clip) const; // as played (warpedit::playedMarkers); empty: none
    double clipFileSeconds (int clip) const;
    int warpMarkerAt (int clip, juce::Point<int> p) const; // the marker's index, or -1
    int warpClip = -1, warpIndex = -1;
    double warpBeat = 0.0; // the dragged marker's beat (shown while dragging, applied on release)
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
public:
    // round 190bv: the clipboard is shared with the clip grid (a clip copied in a slot pastes here and the other way);
    // the stamp tells the grid whether its own copy is still the newest
    static void setCopiedClips (std::vector<Clip> clips)
    {
        clipboard = std::move (clips);
        ++clipboardStamp;
    }
    static const std::vector<Clip>& copiedClips() noexcept { return clipboard; }
    static std::uint64_t copiedStamp() noexcept { return clipboardStamp; }

private:
    void pasteClips (std::int64_t at = -1);
    void normalizeClips (const std::vector<int>& targets, bool asGroup = false);
    juce::PopupMenu buildClipMenu (int index, int mouseX);
    void addKeyFollowItems (juce::PopupMenu& menu, int index, const std::vector<int>& targets); // round 190ds

    // FL's audio clip menu (round 190dz, core/FlClipMenu.h): FL's two columns and sections, LYNX's own items after them
    juce::PopupMenu buildAudioClipMenu (int index, int mouseX);
    std::function<void (float, bool)> clipFitter (int index, std::shared_ptr<const SampleData> sample);
    juce::PopupMenu fitToTempoMenu (int index, std::shared_ptr<const SampleData> sample);
    juce::PopupMenu detectTempoMenu (int index, std::shared_ptr<const SampleData> sample);
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
    juce::Rectangle<int> trackLed (int track) const;   // the mute LED on the right of a header (Simple headers: the M button)
    // the Simple headers' controls (round 184a): empty with FL's headers, a row too short or a track without an insert
    bool simpleHeaders = false;
    bool liveStyle = false;
    bool dragCopies = false; // the clips being moved are copies made at the press
    arrange::TimeClip timeClipboard;
    static constexpr int simpleTrackHeight = 56, simpleTwoRows = 44; // the default row; the height that shows the second row
    juce::Rectangle<int> trackSolo (int track) const;
    juce::Rectangle<int> trackVolume (int track) const;
    juce::Rectangle<int> trackPan (int track) const;
    juce::Rectangle<int> trackFx (int track) const;
    int simpleInsert (int track) const; // the insert a Simple header's controls act on, -1 none
    void selectTrackChannel (int track); // the Simple workflow: a header click selects the track's channel
    int dropLaneTrack() const;          // the row the "drop files here" lane is drawn on, -1 none
    // a level drag on a Simple header: 1 volume, 2 pan (0 none)
    int levelDrag = 0, levelInsert = -1;
    float levelStart = 0.0f;
    juce::Rectangle<int> trackArrow (int track) const; // a group header's fold arrow (empty for other tracks)
    // An audio track's buttons (FL 2026, issue #11), acting on its linked mixer track: "Arm/disarm recording" (left of
    // the LED / arrow) and, on rows tall enough, "Audio input source" (with the monitoring choices). Empty for others.
    juce::Rectangle<int> trackArm (int track) const;
    juce::Rectangle<int> trackInput (int track) const;
    juce::Rectangle<int> trackAudition (int track) const;
    static constexpr int inputButtonMinHeight = 52; // FL shows the row of buttons from 150 % of its track height
    void editTrack (int track, std::function<void (PlaylistTrack&)> change);
    int currentTrack = -1; // the track selected by a click on its header (FL marks it next to the LED)
    std::vector<int> selectedTracks; // Ctrl / Shift+click on headers: more selected tracks (round 165m)
    bool isTrackSelected (int track) const;
    std::vector<int> tracksForMenu (int track) const;
    // round 190dx: FL's new instrument tracks (core trackmodes::addInstrumentTrack; on an instrument track the
    // generator is replaced, FL's EEPLTrackMorePlugMenuClick)
    juce::PopupMenu buildAddTrackMenu();
    void instrumentForTrack (int track, ChannelDef generator);
    void morePluginsForTrack (int track);
    std::unique_ptr<juce::Component> addTrackButton;

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
    bool toolbarFitsNarrow = false; // (round 181a; beside the clip grid only)

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
    static inline std::uint64_t clipboardStamp = 0;
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
    static inline double copiedFadeMs = 0.0; // the fade handle menu's Copy value (round 189i)
    static inline bool hasCopiedFade = false;
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
        bool header = false, led = false, arrow = false, arm = false, input = false, audition = false;
        bool solo = false, volume = false, pan = false, fx = false; // the Simple headers' controls (round 184a)
        int divider = -1; // the track whose bottom edge in the headers is under the mouse (resizes it)
        bool operator== (const Hover& o) const
        {
            return clip == o.clip && track == o.track && target == o.target && header == o.header && led == o.led && arrow == o.arrow
                   && arm == o.arm && input == o.input && audition == o.audition && solo == o.solo && volume == o.volume && pan == o.pan && fx == o.fx
                   && divider == o.divider;
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
    magiclasso::Trail lassoPath; // FL's Magic lasso (round 189g): the mouse trail while the option is on
    void feedMagicLasso (juce::Point<int> p, bool buttonDown);
    bool nudgeWithWheel (juce::Point<int> p, float deltaY); // FL's Shift+wheel nudge (round 189g); true = handled
    double zoomBeforeAll = 0.0, scrollBeforeAll = 0.0; // Ctrl+right-click: zoom to everything and back

    // smooth wheel scrolling
    gesture::Glide glideX, glideY;
    bool gliding = false;
    // `immediate`: a trackpad's smooth scroll moves the view at once (the fingers carry it; round 170e)
    void glideBy (double ticks, double pixels, bool immediate = false);
    void glideFrame();
    juce::VBlankAttachment vblank { this, [this] { glideFrame(); } };
    ui::FrameTicker frames { *this, [this] (double ms) { return liveFrame (ms); } };
    // round 190w: the track headers' level meters (FL's "Show levels on audio / instrument tracks")
    int meteredInsertOf (int track) const;
    bool updateMeterColumn();        // true when the column appeared or went away
    bool stepHeaderMeters (double nowMs);
    int meterColumn = 0;             // px at the right of the headers for the meters (0 = no track shows one)
    std::map<int, std::array<meters::PeakHold, 2>> headerLevels; // per mixer track shown
    double headerMeterMs = -1.0;

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

    // Round 177d (pain point 31a): FL's Playlist menu › Picker panel › Tools items "Select unused › Patterns" and
    // "Sort › Patterns › By name / By color" (EEPickerSelectUnusedPatternsMenu, EEPickerSortPatternsBy*Menu; FL's Audio
    // and Automations entries are left out: the LYNX picker selects patterns only), then LYNX's own "Delete unused
    // patterns" and "Merge identical patterns".
    juce::PopupMenu pickerMenu();

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
        titleRight = right;
        applyTitleInsets();
        resized();
    }
    // the Playlist's toolbar starts after the window title; the pattern picker column (when shown) sits under the title
    void applyTitleInsets()
    {
        playlist.setTitleBarInsets (juce::jmax (0, titleLeft - (pickerHidden ? 0 : PatternPicker::width)), titleRight);
    }

    // The clip grid (Clip-launch workflow, round 181a): not owned, nullptr in Pattern workflow. Beside the Playlist, or
    // alone, or hidden while only the Playlist shows. Small windows give the grid at least what it needs to be usable.
    enum class GridView { beside, grid, song };
    void setClipGrid (juce::Component* grid, GridView view)
    {
        if (clipGrid != nullptr && clipGrid != grid)
            removeChildComponent (clipGrid);
        clipGrid = grid;
        gridView = view;
        if (clipGrid != nullptr)
        {
            addChildComponent (*clipGrid);
            clipGrid->setVisible (view != GridView::song);
        }
        picker.setVisible (clipGrid == nullptr || view != GridView::grid);
        playlist.setToolbarFitsNarrow (clipGrid != nullptr && view == GridView::beside);
        playlist.setVisible (clipGrid == nullptr || view != GridView::grid);
        resized();
    }

    // The Simple workflow (round 184a) has no pattern picker beside the Playlist: its tracks hold their own clips.
    void setPickerHidden (bool hidden)
    {
        if (hidden == pickerHidden)
            return;
        pickerHidden = hidden;
        picker.setVisible (! hidden && (clipGrid == nullptr || gridView != GridView::grid));
        applyTitleInsets(); // (round 189k: in Simple the toolbar went under the title bar's tools by the picker's width)
        resized();
        repaint();
    }

    // A component over everything under the toolbar band (round 184a: the Simple workflow's New track chooser); not
    // owned, nullptr = none.
    void setOverlay (juce::Component* c)
    {
        if (overlay != nullptr && overlay != c)
            removeChildComponent (overlay);
        overlay = c;
        if (overlay != nullptr)
        {
            addAndMakeVisible (*overlay);
            overlay->toFront (false);
        }
        resized();
    }

    // The Detail View (Clip-launch workflow, round 183a): not owned, nullptr in Pattern workflow. Docked at the bottom,
    // under the grid and the Playlist; `heightFor` gives its height out of the panel's.
    void setDetailView (juce::Component* detail, std::function<int (int)> heightFor)
    {
        if (detailView != nullptr && detailView != detail)
            removeChildComponent (detailView);
        detailView = detail;
        detailHeightFor = std::move (heightFor);
        if (detailView != nullptr)
            addChildComponent (*detailView);
        resized();
    }

    // A dock on the right, beside the Playlist (round 189k: the Simple workflow's Finish dock); not owned, nullptr = none.
    void setSideDock (juce::Component* c, int width)
    {
        if (sideDock != nullptr && sideDock != c)
            removeChildComponent (sideDock);
        sideDock = c;
        sideDockWidth = width;
        if (sideDock != nullptr)
            addAndMakeVisible (*sideDock);
        resized();
    }
    juce::Component* getSideDock() const noexcept { return sideDock; }

    void resized() override
    {
        layoutChildren();
        if (overlay != nullptr)
            overlay->setBounds (getLocalBounds().withTrimmedTop (PlaylistComponent::toolbarHeight));
    }

    void layoutChildren()
    {
        auto area = getLocalBounds();
        if (sideDock != nullptr) // under the title row (the window's buttons are there)
            sideDock->setBounds (area.removeFromRight (juce::jmin (sideDockWidth, area.getWidth() / 2)).withTrimmedTop (PlaylistComponent::toolbarHeight));
        if (detailView != nullptr && detailView->isVisible() && detailHeightFor != nullptr)
            detailView->setBounds (area.removeFromBottom (detailHeightFor (area.getHeight())));
        if (pickerHidden)
        {
            picker.setVisible (false);
            playlist.setBounds (area);
            return;
        }
        if (clipGrid != nullptr && gridView != GridView::song)
        {
            // (under the band the title bar shares with the Playlist's toolbar)
            const auto top = titleLeft > 0 ? PlaylistComponent::toolbarHeight : 0;
            if (gridView == GridView::grid)
            {
                clipGrid->setBounds (area.withTrimmedTop (top));
                return;
            }
            clipGrid->setBounds (area.removeFromLeft (juce::jmax (juce::jmin (420, area.getWidth()), area.getWidth() * 45 / 100)).withTrimmedTop (top));
            // a small window: the Playlist's room goes to its tracks; the toolbar's pattern box still picks a pattern
            const auto pickerFits = area.getWidth() >= PatternPicker::width + 480;
            picker.setVisible (pickerFits);
            if (! pickerFits)
            {
                picker.setBounds (area.withWidth (0));
                playlist.setBounds (area);
                return;
            }
        }
        auto column = area.removeFromLeft (PatternPicker::width);
        picker.setBounds (titleLeft > 0 ? column.withTrimmedTop (PlaylistComponent::toolbarHeight) : column);
        playlist.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        if (pickerHidden)
            return; // (the Playlist fills the area)
        // the toolbar band above the picker (and above the grid, when one is beside the Playlist)
        const auto bandWidth = clipGrid != nullptr ? picker.getRight() : PatternPicker::width;
        g.setColour (theme::colour::bg3);
        g.fillRect (0, 0, bandWidth, PlaylistComponent::toolbarHeight);
        g.setColour (theme::colour::border);
        g.fillRect (0, PlaylistComponent::toolbarHeight - 1, bandWidth, 1);
    }

private:
    PatternPicker& picker;
    PlaylistComponent& playlist;
    int titleLeft = 0, titleRight = 0;
    juce::Component* clipGrid = nullptr;
    GridView gridView = GridView::beside;
    juce::Component* overlay = nullptr;
    juce::Component* sideDock = nullptr; // (not owned)
    int sideDockWidth = 0;
    bool pickerHidden = false;
    juce::Component* detailView = nullptr;
    std::function<int (int)> detailHeightFor;
};
} // namespace lynx
