#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ArrangeOps.h"
#include "core/RangeEdit.h"
#include "core/AutoPointEdit.h"
#include "core/InsertMark.h"
#include "core/MagicLasso.h"
#include "core/MeterBallistics.h"
#include "core/TrackHeaderGestures.h"
#include "core/PickerSelection.h"
#include "core/PlaylistOps.h"
#include "core/PlaylistViewOptions.h"
#include "core/StripSilence.h"
#include "core/TrackClips.h" // round 190ne
#include "core/SimpleHeaderTarget.h" // round 190oo (CS-24)
#include "engine/AudioEngine.h"
#include "ui/DeleteFade.h"
#include "ui/EditorTools.h"
#include "ui/FrameTicker.h"
#include "ui/MotionOverlay.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
struct SampleData; // (core/Instruments.h)
namespace loopinfo
{
class Shared; // (ui/LoopInfo.h, round 190kx)
}
// (shots, round 190el) the audio clip's Slice to New MIDI Track dialog for clip `clipIndex`, built but not shown, with
// "Create one slice per" at item `per` (rack::SlicePer)
std::unique_ptr<juce::Component> makeSliceDialogForSnapshot (AudioEngine& engine, int clipIndex, int per);

class PatternBlocksView;
class EffectsPanel;
class PatternPicker;
class VideoLanesView;
class SectionsLaneView;
class ChordLaneView;

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
        // issue #175 (FL's rule): the clip last clicked with the Pencil or the Brush, as it is (core/PaintTemplate.h); a
        // choice in the Picker (a new Brush) paints the plain item again
        std::optional<Clip> picked;
    };
    Brush brush;
    int knownCurrentPattern = -1;

    explicit PlaylistComponent (AudioEngine& engine);
    juce::String cacheSelfTest (const std::function<void()>& waitForWaveforms, const juce::File& dumpFolder = {}); // --cachetest (round 174f)
    ~PlaylistComponent() override;
    void visibilityChanged() override;

    std::function<void()> onOpenPattern;
    std::function<void()> onOpenPianoRoll;
    // round 190qp: a pattern clip on an instrument track double-clicked in Track-only mode or the Simple layout opens its
    // instrument's notes (core/TrackOnly.h pianoRollChannelOf): the Simple dock's Editor tab, else the Piano roll. The
    // channel is selected and the clip's pattern made current before the call.
    std::function<void (int track)> onOpenTrackClipNotes;
    std::function<void (int pattern)> onRenamePattern;  // clip menu: Rename and color (pattern clips)
    std::function<void (int channel)> onOpenInSampler;  // clip menu: the audio clip's file in a new Sampler channel
    // round 190dx: a new instrument track's channel was made (or its generator replaced): the main window opens it as the
    // Channel Rack opens a new generator
    std::function<void (int channel, InstrumentKind kind)> onGeneratorAdded;
    // FL's "+" at the top left of the track headers (AddInstrBtn, round 190dx): left click = this menu ("Audio track":
    // Add audio track; "Instrument track": More plugins..., the generators), right click = addAudioTrackNow
    juce::PopupMenu getAddTrackMenuForSnapshot() { return buildAddTrackMenu(); }
    juce::PopupMenu trackMenuForSnapshot (int track) { return buildTrackMenu (track); }            // (round 190mp)
    void autoNameClipsForSnapshot (int track) { autoNameClipsFromMenu (track, { track }); }        // (round 190mp, DE-127)
    const ToolStrip& toolsForSnapshot() const noexcept { return tools; }                           // (round 190mp)
    juce::Component* getAddTrackButtonForSnapshot() const noexcept { return addTrackButton.get(); }
    int addAudioTrackNow(); // FL's Add audio track: the new track (selected, in view), −1 when none could be made

    // Double-click on a pattern clip opens it in: 0 = the Piano Roll when it has piano-roll notes, else the
    // Channel Rack; 1 = always the Channel Rack; 2 = always the Piano Roll. Alt+double-click opens the other one.
    static inline int editPatternsIn = 0;
    // FL 2026's Playlist menu › View › Track headers (round 190w; FL's defaults): the record / input buttons on audio
    // tracks, and a stereo level meter beside the header of audio tracks (their mixer track) and instrument tracks
    // (their channel's mixer track). Kept with the UI settings.
    static inline bool showAudioTrackControls = true, showAudioTrackLevels = true, showInstrumentTrackLevels = false;
    // FL's Playlist menu › View › "Hide collapsed grouped tracks" (EEHideCollapsedAction, HideCollapsedSubTracks; FL manual:
    // "Collapsed grouped tracks will be hidden"; round 190jf): the members of a collapsed group get no row at all
    static inline bool hideCollapsedGroups = false;
    // Round 190op (P4, LYNX extra): the Playlist menu's "Show hidden tracks": hidden tracks (PlaylistTrack::hidden) get
    // their rows back, drawn dimmed with a crossed eye, so they can be shown again; the Clip-launch grid shows their
    // columns. A view preference (saved with the UI settings).
    static inline bool showHiddenTracks = false;
    // #230 (round 190nq): FL 2026's Playlist menu › View › "Show fade previews" (EEEnablePreviewFadesAction, on by default,
    // [SHOT] 190nq f08c / f12c / f13c): an audio clip's waveform is drawn with its fades applied
    static inline bool showFadePreviews = true;
    // round 190oo (design review O-1, a LYNX extra, off by default): Playlist › View › "Shade faded audio (LYNX)" — the
    // Pattern layout also shades what a fade takes away, as the Clip-launch layout and issue #230's reference (Live) do;
    // FL 2026 draws only the curve (and the fade preview). Kept with the UI settings ("shadeFadedAudio").
    static inline bool shadeFadedAudio = false;
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
    void splitSelectionForLive(); // round 190im (R12): Live's Ctrl+E Split of the selected clips
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
    // `clipMenu` (round 190ey): the clip menu's "Fade in / out curve" submenu (core flclipmenu::fadeCurve)
    juce::PopupMenu fadeHandleMenu (int index, int side, bool clipMenu = false);
    // FL 2026's fade tension handle menu (round 190jz, issue #194; FL's right-click on the circle of a fade): Reset fade
    // in / out tension, Create crossfade, Copy value, Paste value, Type in value... (-1..1 or %), Curve shape
    juce::PopupMenu fadeTensionMenu (int index, int side);
    // --issueshot190jz: the fade handles as the mouse shows them (hover over clip `index`, `target` 0 = the clip, 1 / 2 = the
    // fade in / out handle, 3 / 4 = the fade in / out tension handle), and the hint a drag of one of them posts
    void hoverFadeForSnapshot (int index, int target);
    ui::Hint fadeDragHintForSnapshot (int index, int target);
    // round 190mr (DL-68): clip `index`'s fade-out handle (fadeIn: its fade-in handle) dragged to `tick`, as the mouse does
    bool crossfadeDragForSnapshot (int index, std::int64_t tick, bool fadeIn = false, bool newDrag = true)
    {
        drag = fadeIn ? Drag::fadeIn : Drag::fadeOut;
        dragClip = index;
        if (newDrag)
            crossPair = -1;
        const auto made = crossfadeDragOver (tick);
        drag = Drag::none;
        return made;
    }
    juce::Point<float> fadeHandlePointForSnapshot (int index, int side) const { return fadeHandlePoint (index, side); }
    juce::Point<float> fadeTensionPointForSnapshot (int index, int side) const { return fadeTensionPoint (index, side).value_or (juce::Point<float> { -1.0f, -1.0f }); }
    // FL's gain handle menu of clip `index` (round 166e; Normalize items round 189n): what showGainMenu opens; public for
    // --snapshot, which also sets the selection it reads (selectForSnapshot)
    juce::PopupMenu gainMenu (int index);
    void selectForSnapshot (std::vector<int> indices) { setSelection (std::move (indices)); }
    juce::PopupMenu selectMenu();
    // The shortcut text of a key map action for menus ("\tShift+C"; empty when unbound), set by the main window.
    static inline std::function<juce::String (const std::string& id)> shortcutText;
    // The Playlist's main menu (its window's ▾ items), for FL's middle + right click tool menu (round 190mp, DE-62).
    std::function<juce::PopupMenu()> mainMenu;
    // Issue #170 (round 190ib): turns the project's "Link Playlist tracks to mixer tracks" on or off. Switching on first
    // lists what it will change (core/PlaylistOps.h autolink::planOn: inserts moved, channels routed, tracks that only
    // repeat others) and asks when it changes more than links; it is one undo step. `done (applied)` runs after.
    static void switchLinkTracks (AudioEngine& engine, bool on, std::function<void (bool applied)> done = {});
    // The confirmation's text for a plan (tests and --linkshot190ib read it).
    static juce::String describeLinkPlan (const Song& song, const autolink::Report& report);
    // Tests: answers the confirmation instead of the dialog (null: the dialog).
    static inline std::function<bool (const autolink::Report&)> answerLinkConfirmation;
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
    // (round 190im: the drop ghost; round 190jc: the Simple drop lane lights while it is under the drag)
    void itemDragEnter (const SourceDetails& details) override { itemDragMove (details); }
    // --details190imshot: a drag of `description` held over `at` (the ghost drawn), then let go (cleared)
    void dragOverForSnapshot (const juce::var& description, juce::Point<int> at);
    void dropForSnapshot (const juce::var& description, juce::Point<int> at) { itemDropped (SourceDetails (description, nullptr, at)); } // (round 190jj)
    juce::String dropGhostReasonForCheck() const { return dropGhostReason; }
    bool dropGhostTagOnlyForCheck() const noexcept { return dropGhostTagOnly; }
    juce::Rectangle<int> dropGhostAreaForCheck() const { return dropGhostArea; }
    bool isClipSelectedForCheck (int clip) const { return isSelected (clip); }
    // --details190imshot: the hint an automation clip's point (or its segment's tension) drag shows, as the drag makes it
    ui::Hint dragHintForSnapshot (int clip, int point, bool tension);
    bool followPausedForSnapshot() const { return EditorGlobals::get().isFollowPaused (EditorGlobals::FollowView::playlist); }
    double scrollTicksForSnapshot() const noexcept { return scrollTicks; }
    // round 190jc (design190ia SG-6): the Simple drop lane lights while files or Browser items are dragged over it
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    // issue #152 point 6: a generator / effect from the Browser's Plugin database (or the Plugin picker). Pattern
    // workflow, FL-exact: on a track header only (core/PluginDrop.h); Clip-launch workflow: on a track, as Live
    void pluginDropped (const SourceDetails& details);
    void applyGeneratorDrop (ChannelDef generator, std::vector<int> tracks, bool shift);
    // Issue #177: entries dragged out of the Picker panel (patterns, audio clips, automation clips), placed as FL does
    // (core/PickerDrop.h): entry k on the track under the mouse + k at the snapped mouse time; Shift: one after the other
    // on that track. `at` is in this component's coordinates.
    static juce::String pickerDragDescription (ClipKind kind, const std::vector<int>& entries);
    static bool pickerDrag (const juce::var& description, ClipKind& kind, std::vector<int>& entries);
    void dropFromPicker (ClipKind kind, const std::vector<int>& entries, juce::Point<int> at, bool shift);
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
    // round 190ig (--design190igshot): the vertical scroll in px, and one frame step (the playhead, FL's activity column)
    void setScrollYForSnapshot (int y) { scrollY = juce::jmax (0, y); updateScrollBars(); repaint(); }
    // round 190nr: the site / Store picture "-playlist-fit.png" (the song zoomed to fit, as before 0.1.30's FL open zoom),
    // then the view as it was
    struct ViewForSnapshot { double pixelsPerTick, scrollTicks; int scrollY; };
    ViewForSnapshot viewForSnapshot() const noexcept { return { pixelsPerTick, scrollTicks, scrollY }; }
    void zoomAllForSnapshot() { zoomAll(); }
    void restoreViewForSnapshot (const ViewForSnapshot& v) { pixelsPerTick = v.pixelsPerTick; scrollTicks = v.scrollTicks; scrollY = v.scrollY; updateScrollBars(); repaint(); }
    void frameStepForSnapshot() { liveFrame (juce::Time::getMillisecondCounterHiRes()); }
    // round 190ef (--drawshot190ef): where a lane's curve is drawn, and the x of a tick
    juce::Rectangle<float> laneBodyForSnapshot (int track, int lane) const { return laneBody (track, lane); }
    juce::Rectangle<int> trackRowForSnapshot (int track) const { return { 0, trackToY (track), getWidth(), heightOf (track) }; } // (round 190fc)
    void setTrackHeightsForTest (std::vector<int> tracks, int height) { setTrackHeights (std::move (tracks), height); } // (round 190ng, issue #222)
    std::vector<int> selectedTracksForSnapshot() const { return sortedTrackSelection(); } // (round 190ft)
    int numTracksForSnapshot() const { return numTracks(); } // (round 190gw)
    juce::Rectangle<int> trackLedForSnapshot (int track) const { return trackLed (track); } // (round 190jf)
    int headerWidthForSnapshot() const noexcept { return headerWidth; }
    // round 190jq (core/PlaylistViewOptions.h): the layout's own header width (without the dragged extra), the rows as
    // drawn (automation clips as lanes), and the switches' state read again (after Settings, the menu or a key)
    int headerBaseWidthForSnapshot() const noexcept { return headerBaseWidth; }
    const cliplanes::Projection& projectionForSnapshot() const noexcept { return projection; }
    juce::Component& pointerButtonForSnapshot() noexcept { return pointerButton; }
    void refreshViewOptions();
    double playheadTickForSnapshot() const noexcept { return playheadTick; }
    void setPlayheadForSnapshot (double tick) { engine.jumpTo (tick); playheadTick = tick; } // (round 190on: as a ruler click, stopped)
    juce::Rectangle<int> trackLaneArrowForSnapshot (int track) const { return trackLaneArrow (track); }
    double xOfTickForSnapshot (double tick) const { return tickToX (tick); }
    juce::Rectangle<int> clipBoundsForSnapshot (int clip) const { return clipBounds (clip); } // (round 190gi)
    ui::DeleteFade& deleteFadeForSnapshot() noexcept { return deleteFade; } // (round 190oq, issue #250)
    void deleteSelectionForSnapshot() { deleteSelection(); }
    // round 190nx (--demo, ui.clipmenu): selects the clip and opens its context menu at the clip, as a right-click on it does,
    // with no mouse needed. Returns the clip's rectangle on screen, or an empty one when the clip is not in view
    juce::Rectangle<int> openClipMenuForDemo (int clip);
    juce::Rectangle<float> curveBodyForSnapshot (int clip) const; // (round 190gi: where an envelope / automation curve is drawn)
    double warpStripSecondsForSnapshot (int clip, juce::Point<int> p) const { return warpStripSeconds (clip, p); }
    std::size_t warpMarkerCountForSnapshot (int clip) const { return clipWarpMarkers (clip).size(); }
    double clipFileSecondsForSnapshot (int clip) const { return clipFileSeconds (clip); }
    juce::PopupMenu getClipMenuForSnapshot (int index); // --snapshot: the clip menu of that clip
    // Round 190hp (slice B18, core/StripSilence.h): the audio clip menu's "Split at silence..." — these audio clips are
    // cut where they stay below the threshold and the quiet parts go (one undo step). Returns the clips made (0: nothing
    // to strip); `why` gets the hint shown. `canSplitAtSilence`: the clip is audio, its sample loaded and not warped.
    int splitClipsAtSilence (const std::vector<int>& targets, const stripsilence::Options& options, juce::String* why = nullptr);
    bool canSplitAtSilence (int index);
    static stripsilence::Options& splitSilenceOptions(); // the dialog's last values (this run of LYNX)
    std::unique_ptr<juce::Component> makeSplitSilenceDialogForSnapshot (int index);
    juce::PopupMenu getAutomationPointMenuForSnapshot (int clip, int point); // FL's point menu of an automation clip's point (round 190bw)
    std::vector<int> selectedPointsForTests() const { return selectedPoints; } // issue #207 (round 190lw)
    std::vector<int> clipSelectionForTests() const { return selection; }
    std::int64_t gridForSnapshot() const { return gridTicks(); } // round 190mq
    bool penCursorForTests (juce::Point<int> p) const { return curveSpotAt (p).valid(); }
    juce::Rectangle<float> laneBodyForTests (int track, int lane) const { return laneBody (track, lane); }
    juce::Point<int> curvePointForTests (int clip, int point) const;          // a clip curve's point on screen
    juce::Point<int> curveSpotXYForTests (int clip, std::int64_t tick, float value) const; // a place in a clip's curve area
    juce::PopupMenu getTrackMenuForSnapshot (int track) { return buildTrackMenu (track); }
    // hovers the right edge of the first clip in view (grip, outline, menu button); returns its hint
    ui::Hint hoverForSnapshot();

    // The track headers' width: FL's 96 px, the Simple workflow's wider headers (round 184a), or the linked headers
    // (round 190gz). updateHeaderWidth sets it; everything else only reads it.
    static constexpr int flHeaderWidth = 96, simpleHeaderWidth = 232, linkedHeaderWidth = 152;
    static constexpr int simpleHeaderWidthLarge = 272; // round 190jc: the Simple headers with Large controls
    // round 190ig (FM-23): the Pattern layout's FL headers — FL's 98 px plates + the 8 px selection / activity column
    static constexpr int flPatternHeaderWidth = 106, flTallHeader = 40;
    static int flHeaderWidthNow() noexcept;
    bool flHeaders() const noexcept;
    bool liveHeaders() const; // round 190jn: Live's header row (the Live look, not the Simple or the linked headers)
    std::vector<char> activityShown; // round 190ig (FM-28): [track] its activity strip lit (FL's Pattern layout, while playing)
    // Issue #154 (round 190ht; core/TrackInserts.h), a LYNX extra, off by default (FL keeps an insert's effects in the
    // Mixer): the linked headers get a column listing their mixer track's insert effects, and are this much wider.
    static inline bool showTrackInserts = false; // ui-options.txt trackInserts, action playlist.trackInserts
    static constexpr int insertsColumnWidth = 104;
    juce::Rectangle<int> insertsListForSnapshot (int track) const { return trackInserts (track); }
    // round 190im (R5, DC-7): the lit strip's brightness step 1..4 follows the level of what the track plays (its clips'
    // mixer tracks, read through the meters' activity reader); a track without audio (automation) lights at 4
    std::map<int, std::pair<float, std::uint32_t>> activityInsertLevels; // [mixer track] the held level, the frame it was read in
    std::vector<std::vector<int>> patternInserts; // [pattern] the mixer tracks its channels go to (rebuilt on a song change)
    std::uint64_t patternInsertsStamp = ~(std::uint64_t) 0;
    double activityMs = -1.0, activityElapsedMs = 0.0;
    std::uint32_t activityFrame = 0;
    float activityLevelOf (const Song& song, const Clip& clip);
    // round 190im (R5, DC-3): the picker beside this Playlist (it registers itself) and the patterns it shows as playing
    PatternPicker* livePicker = nullptr;
    friend class PatternPicker;
    // round 190im (R5, DC-5): the playhead's glow trail (Animations on, while playing) as last painted
    bool trailShown = false;
    // round 190im (R15): what the frame step saw of the view (a change it did not make = a scroll by hand), and the
    // song's publish count at the last mouse press (a gesture that changed it = an edit)
    double followSeenScroll = 0.0, followSeenZoom = 0.0;
    std::uint64_t followEditStamp = 0;
    static constexpr int playheadTrail = 14; // (FL: ≈14 px of fading glow behind the line, shots/fl-playhead-glow-zoom.png)
    bool playheadTrailOn() const;
    // round 190im (R5, DE-135): the clips a drag over the Playlist would place, drawn where they would land
    std::vector<Clip> dropGhost;
    juce::Rectangle<int> dropGhostArea;
    void updateDropGhost (const SourceDetails& details);
    void clearDropGhost();
    void paintDropGhost (juce::Graphics& g);
    juce::Rectangle<int> dropGhostBounds() const;
    juce::String dropGhostFile; // (an audio file's length, read once per drag)
    std::int64_t dropGhostFileTicks = 0;
    // Round 190jj (capture190ji): the ghost per layout. Pattern (FL, C-11): a borderless white block at ~13 % in the row,
    // as long as the clip. Simple (BandLab): a grey dashed region with the item's name tag at its lower left; over a track
    // that can't take it, a red region with the reason (and the drop does nothing there); below the drop lane only the
    // tag follows the mouse. Clip-launch keeps 190im's coloured ghost.
    juce::String dropGhostName, dropGhostReason;
    juce::Point<int> dropGhostMouse;
    bool dropGhostTagOnly = false;
    void finishDropGhost (std::vector<Clip> ghost, juce::Point<int> mouse, const juce::String& name);
    void updateDropGhostForFiles (const juce::StringArray& files, juce::Point<int> p);
    std::int64_t dropGhostAudioTicks (const juce::File& file);
    juce::Rectangle<int> dropGhostTag() const;
    // Simple: why a track refuses what is dropped on it (empty: it takes it); BandLab's "Region not supported by this
    // track" for audio on a track of notes (instrument, drums, sampler, pads)
    juce::String simpleDropRefusal (int track, ClipKind kind) const;
    int headerWidth = flHeaderWidth;
    static constexpr int toolbarHeight = 32; // the panel title row (Workspace::titleHeight)
    static constexpr int overviewHeight = 16; // the overview strip under the toolbar (round 90)
    static constexpr int timelineHeight = 24;
    static constexpr int rulerTop = toolbarHeight + overviewHeight;
    static constexpr int headerHeight = rulerTop + timelineHeight;
    // the default row (each track can be resized: P18): the song's (FL's 48 px since round 190gw; 38 in older projects);
    // the Clip-launch layout keeps LYNX's 38, the Simple layout its own
    static constexpr int liveTrackHeight = 38;
    int baseRowHeight() const;
    static constexpr int foldedHeight = 7; // a member of a collapsed group: a thin row of clip bars (as FL)
    static constexpr int groupIndent = 8;  // grouped tracks' headers are indented under their group header
    static int headerIndent (const Song& song, int track); // round 190jf: groupIndent per folder level
    static constexpr int scrollBarSize = 12;

    static juce::Colour patternColour (const Song& song, int pattern);
    // round 190jc: a clip's colours in the Simple layout (BandLab's track hue; nullopt in the other layouts)
    std::optional<theme::ClipLook> simpleClipLook (const Clip& c) const;

    // The Simple workflow's track headers (round 184a, slice S4; core/SimpleTracks.h): wider headers whose controls act
    // on the track's mixer insert (mute, solo, record arm, volume, pan, "+FX"), taller rows by default, a lane under
    // the tracks to drop files on, and a click that also selects the track's channel. Off: FL's headers, unchanged.
    void setSimpleHeaders (bool on);
    // Round 190jr (BUILD-LIST 13, BandLab's automation switch): the Simple layout's automation view — each Simple track's
    // row shows one parameter's line over its dimmed regions (the header's menu picks it: Volume, Pan, the insert's
    // effect parameters); a click on the line adds a point. Turning it on changes nothing in the song: a lane is made
    // only when a point is first added. A view state, not saved.
    void setSimpleAutomation (bool on);
    bool isSimpleAutomation() const noexcept { return simpleAutomation; }
    std::function<void (bool)> onSimpleAutomationChanged;
    juce::PopupMenu getSimpleAutoMenuForSnapshot (int track) { return buildSimpleAutoMenu (track); }
    void setSimpleAutoTargetForSnapshot (int track, AutomationTarget target) { simpleAutoTargets[track] = target; repaint(); }
    // The Clip-launch workflow (round 190cg): track fold, "Group with above track" makes the group a bus, and automation
    // follows clips unless set otherwise. The data is the Song's either way; the other layouts show it all the same.
    void setLiveStyle (bool on);
    bool hasLiveStyle() const noexcept { return liveStyle; }
    static constexpr int liveFoldedHeight = 17; // a folded track: its title bar's height
    // Round 190jn (design190ia CL-17, CL-18, owner decision CL-17): the Arrangement in Live 12.4's look while the
    // Clip-launch layout shows it (theme::livelook::active(); the classic look keeps round 190cg's): Live's 19 px collapsed
    // lanes (17 px + a 2 px separator) by default, LYNX's 38 px with View › "Tall Arrangement lanes"
    // (playlistTallLiveLanes()); clips as colour blocks with dark names and waveforms; Live's track header row.
    static constexpr int liveCollapsedHeight = 19;
    // the Live headers: Live's 2 px separator, its 103 px name box in the track colour, a 2 px separator, then the
    // controls panel (#363636) with the 46 px activator, and LYNX's 9 px selection / meter column
    static constexpr int liveHeaderWidth = 170;
    bool liveArrangeLook() const noexcept;
    // round 190mk (DL-70): Live's insert marker after a click on an empty spot of a lane (the line, the status bar text)
    bool insertMarkForSnapshot (int track, std::int64_t tick, juce::Rectangle<int>& line, juce::String& hint);
    // after View › "Tall Arrangement lanes" or the classic look changed: the rows, the header width and the picture again
    void liveLanesChanged();
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
    // round 190ft (FL FUN_012e3aa0): a header click made an audio / instrument track the only selected one: its mixer
    // track (the main window makes it the mixer's current track when the mixer is showing)
    std::function<void (int insert)> onLinkedMixerTrack;
    // Hotfix 0.1.31.1 (kainproctor, Discord): wherever a Device View shows (the Clip-launch layout, with Live's look or the
    // classic one) a track chosen here — its
    // header clicked alone, a clip or an empty spot of its lane clicked — is the selected track (Live 12 manual: the Device
    // View shows the selected track's devices; one selection for Session and Arrangement). An automation lane chooses its
    // owner track.
    std::function<void (int track)> onArrangementTrackChosen;
    void chooseArrangementTrack (int track); // (also the tests' way in)
    // Files dropped below the tracks (or MIDI files anywhere) while the Simple headers are on: new tracks from `tick`
    std::function<void (const juce::StringArray& files, std::int64_t tick)> onSimpleFilesDropped;
    // round 190jc (design190ia SG-2): the round "+" under the Simple tracks (the New-track chooser, as "+ Add track")
    std::function<void()> onSimpleAddTrack;
    juce::Rectangle<int> simpleAddButtonForSnapshot() const { return simpleAddButton(); }
    void setDropLaneHotForSnapshot (bool hot) { setDropLaneHot (hot); }
    // round 190jc: View › Large controls changed (the Simple headers' sizes and row heights)
    void simpleLookChanged();
    // round 190fo: the Simple workflow's track menu "Export this track" › As audio / As MIDI (nullptr: not offered)
    std::function<void (int track, bool asMidi)> onExportTrack;

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
    int tracksTop() const { return headerHeight + sectionsLaneHeight() + chordLaneHeight() + videoSectionHeight(); } // the clip tracks' top edge
    // The Sections lane (round 190ol, core/Sections.h; owner decision O-1): a thin lane under the ruler with the song's
    // sections. Simple: shown once the song has a section (the title bar's "Sections" button adds the first one, or hides
    // and shows it again); Pattern: the LYNX extra "Sections lane" (Playlist menu › View), off by default; Clip-launch: off.
    bool sectionsLaneShown() const;
    int sectionsLaneHeight() const; // 0 when hidden
    static constexpr int sectionsLaneFlHeight = 18, sectionsLaneSimpleHeight = 44;
    void toggleSimpleSections();   // the Simple title bar's "Sections" button
    void sectionsLaneChanged();    // the extra switched, a layout change: lay the lane out again
    SectionsLaneView* getSectionsLaneForSnapshot() { return sectionsLane.get(); }
    // The Chord lane (round 190pf, core/ChordLane.h): chords over the song under the ruler (under the Sections lane when it
    // shows). A LYNX extra in every layout, off by default (Playlist menu › View, Settings › LYNX extras; the Simple ruler's
    // menu too).
    bool chordLaneShown() const;
    int chordLaneHeight() const; // 0 when hidden
    static constexpr int chordLaneFlHeight = 18, chordLaneSimpleHeight = 30;
    void chordLaneChanged();     // the extra switched, a layout change: lay the lane out again
    ChordLaneView* getChordLaneForSnapshot() { return chordLane.get(); }
    // Round 190pf (research190oe P12): the audio clip menu's "Make the project tempo follow this clip..." (Smart Tempo)
    void followClipTempo (int clip);
    VideoLanesView* getVideoLanesForSnapshot() { return videoLanes.get(); }
    static juce::Colour audioClipColour (const ChannelDef& channel);

    // A project was just opened: scroll to the start and, when the song is longer than the view, zoom out
    // to show all of it (FL opens a project with the arrangement in view). Waits for a size if not laid out.
    void showSongAfterOpen (bool newProject = false); // (round 190oj: File › New: FL's new-project zoom, ~60 px a bar)

    // The track selected by a header click (FL's mark next to the LED), −1 = none; selectTrack selects one track as a
    // click on its header does, without the menus, and scrolls it into view (round 184a, S4: a new Simple track; S16:
    // the game controller's D-pad).
    int getCurrentTrack() const noexcept { return currentTrack; }
    void selectTrack (int track);
    juce::Rectangle<int> gridAreaForHints() const { return gridArea(); } // (round 190np: the empty-state hint goes over it)
    // Several tracks selected, the first current and scrolled into view (round 190fl: the mixer's "Select › Channels and
    // playlist tracks routed to this track")
    void selectTracks (const std::vector<int>& tracks);
    const std::vector<int>& getSelectedTracks() const noexcept { return selectedTracks; }

private:
    enum class Drag { none, move, resize, trimLeft, slip, erase, seek, loop, select, fadeIn, fadeOut, paint, muting, knife, zoomRange, listen, pan, timeMove, timeShift, autoPoint, autoTension,
                      lanePoint, laneTension, laneDraw, trackResize, headerSelect, preview, trackZoom, heightZoom, timeZoom, perfLaunch, clipGain, swipe, warpMarker,
                      tempoMarker, headerMove, regionLoop, headerWidth, fadeTension,
                      pointBand, pointsMove, pen, // (round 190lw, issue #207)
                      range, rangeMove }; // (round 190pa, P14)
    void swipeTimeSelectionToComp (int lane);
    // round 190mr (DL-68): Live's crossfade drag-over (the neighbour found when the handle passes the edge, both clips as
    // they were then: earlier first)
    bool crossfadeDragOver (std::int64_t tick);
    int crossPair = -1;
    std::array<Clip, 2> crossOriginals {};
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
    // Round 190jq (core/PlaylistViewOptions.h, all LYNX extras). #191: the pointer-click switch beside Stretch (shown with
    // its Settings › LYNX extras switch); a pointer press on empty space (its release moves the song position); a double
    // click that placed a clip (its double-click action, opening the clip, is skipped).
    IconButton pointerButton { "pointerClick", Icon::pointer };
    bool pointerPress = false, placedByDoubleClick = false;
    // #164: the header column's width before the dragged extra, and the extra when a drag started; the grip is lit
    int headerBaseWidth = flHeaderWidth, headerWidthDragStart = 0;
    bool headerGripLit = false;
    bool onHeaderGrip (juce::Point<int> p) const;
    // #191.3: automation clips drawn as lanes under their owner track (a projection of the rows; the song is untouched)
    cliplanes::Projection projection;
    std::uint64_t projectionStamp = ~(std::uint64_t) 0;
    std::vector<juce::String> laneNames; // [track] the name a lane's header shows (its clips' automation)
    bool clipLanesShown() const noexcept;
    void updateProjection (int count);
    bool laneHidden (int owner) const; // the owner's lanes are folded away (its arrow, a folded track or group)
    void paintClipLaneHeader (juce::Graphics& g, int track, int y, int height, bool hovered);
    // Round 190oo (CS-19): Live's look draws each automation clip lane as one envelope (a 2 px #ff4d47 line, 6 px hollow
    // breakpoints) over the lane's whole length instead of clip blocks. The lanes' clips in time order are worked out with
    // the projection (cliplanes::laneClipLists), so a paint only walks the visible rows' lists.
    std::vector<std::vector<int>> laneClips;
    void paintLiveEnvelopeLanes (juce::Graphics& g, juce::Rectangle<int> grid);
    // Round 190pb (rollup 0.1.31 step-5 paint regression: Patience's Clip-launch arrangement +72 % instructions a frame):
    // each envelope lane's drawing worked out once for the grid's whole width and kept while nothing it shows changes (the
    // song, the lane lists, the zoom, the scroll, the lane's place and height, the grid, the display scale): the line as
    // its stroke outline, the flat stretches, the rings. A frame then fills them, the pixels of stroking and drawing them
    // as before (JUCE strokes a path by filling the outline createStrokedPath makes; a ring is two circles, even-odd). A
    // partial repaint (the playhead's columns) works out its own part as before: filling a whole lane's outline to show
    // 14 px of it cost three times the old column.
    struct EnvelopeLane
    {
        int track = -1, y = 0, h = 0;
        std::vector<juce::Rectangle<float>> flats;
        juce::Path stroke, rings;
    };
    struct EnvelopeLaneCache
    {
        std::uint64_t publish = ~(std::uint64_t) 0, projection = ~(std::uint64_t) 0;
        double pixelsPerTick = 0.0, scrollTicks = 0.0;
        juce::Rectangle<int> grid;
        float scale = 0.0f;
        std::vector<EnvelopeLane> lanes; // [track], track -1: not made yet
    } envelopeCache;
    // `from` / `to`: the x range the line is worked out for (a whole frame: the grid's; a partial repaint: its part)
    void makeEnvelopeLane (EnvelopeLane& lane, int track, int y, int h, juce::Rectangle<int> grid, float from, float to, float scale) const;
    // Round 190oo (CS O-3): the engine's open count the lanes were last folded for (cliplanes::foldedOnOpen): a project
    // opened is folded the first time the Clip-launch arrangement shows it
    std::uint64_t foldedForOpen = 0;
    void foldLanesAfterOpen();
    // Round 190oo (CS-20): a take lane under Live's headers has no activator / number box (Live's take-lane header is
    // the name and the audition speaker only)
    bool liveTakeLane (int track) const;
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
        juce::RectangleList<int> stickyTitles; // round 190ig: FL's clip titles at the left edge (header bands) to paint again
        // round 190im (perf): the same bands where the clip alone covers them (its header is opaque): repaired by painting
        // that one clip in its band, not by a whole exact paint pass (clip index, band)
        std::vector<std::pair<int, juce::Rectangle<int>>> heldBands;
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
    void paintStickyTitles (juce::Graphics& g);
    void paintHeldClip (juce::Graphics& g, int index); // round 190im: one audio / automation clip as paint() draws it
    struct HeldTitlesKey // round 190ig: what the held clip titles were last found for
    {
        double scrollTicks = -1.0, pixelsPerTick = 0.0;
        int scrollY = 0;
        std::uint64_t published = 0;
        std::size_t clips = 0;
        juce::Rectangle<int> grid;
        bool operator== (const HeldTitlesKey&) const = default;
    } heldTitlesKey;
    std::vector<int> heldTitles;
    void paintPlayhead (juce::Graphics& g);
    void paintPlayheadAt (juce::Graphics& g, float x);
    // round 190ip: while the song plays, the playhead is moved by the GPU over the view (ui/MotionOverlay.h) instead of
    // repainting its column every frame; stopped (or when the overlay cannot show it) it is painted as before
    ui::MotionOverlay gpuPlayhead { *this, [this] (juce::Graphics& g, float x) { paintPlayheadAt (g, x); }, true };
    ui::MotionOverlay::Mark playheadMark() const;
    juce::Rectangle<int> playheadColumn (double tick, int left = 8) const; // (left: the trail's reach, round 190im)
    void repaintPlayheadColumn (double tick);
    void paintPlayheadTrail (juce::Graphics& g, float x, juce::Rectangle<int> grid) const; // round 190im (R5, DC-5)
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
    int defaultRowHeight() const { return scaledHeight (baseRowHeight()); }
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
    // round 190ft (FL AUTracksPanelMouseDown / MouseMove): the band press, a drag across the headers after a Ctrl /
    // Ctrl+Shift press (adds or removes the tracks crossed; with Ctrl the clips of the tracks crossed are selected)
    std::vector<int> bandTargets (int track) const; // the track, and the other selected tracks when it is one of them
    void bandToggleLock (int track);
    void bandReset (int track);
    std::vector<int> sortedTrackSelection() const;
    void selectClipsOnTracks (int fromTrack, int toTrack, bool add);
    void followLinkedTrack (int track); // FL's select-only side effects (instrument channel, mixer track)
    int headerDragAnchor = -1;
    bool headerDragAdding = true, headerDragClips = false, headerCtrlPending = false;
    // round 190jf (B17): Alt+drag of a header moves the track with its folder (core/FolderDrag.h); where it would land
    int headerMoveTrack = -1, headerMoveStartX = 0;
    int headerMoveBefore = -1, headerMoveDepth = 0; // (−1: no move yet)
    void paintFolderDrop (juce::Graphics& g);
    std::vector<int> headerClipBase;
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
    Clip makeSourceClip (ClipKind kind, int index, int track, std::int64_t start) const; // a Picker entry's plain clip
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
    // Round 190jz (issue #194, FL 2026 GUI): where an audio clip's content sits (under its header, as paintAudioClip draws
    // it), the fade handles' corners there and the fades' tension handles (the circle at the middle of a fade's curve)
    juce::Rectangle<float> audioClipBody (int clip) const;
    juce::Point<float> fadeHandlePoint (int clip, int side) const;  // the body's top corner at the end of the fade
    std::optional<juce::Point<float>> fadeTensionPoint (int clip, int side) const; // none: no fade, or a crossfade
    int fadeTensionAt (int clip, juce::Point<int> p) const; // 1 = fade-in's, 2 = fade-out's, 0 = none
    juce::Colour fadeInk (int clip) const; // the waveform's colour (FL draws the handles and curves in it)
    void paintFadeHandles (juce::Graphics& g, int clip);
    juce::String fadeLengthText (std::int64_t ticks) const; // FL's "0'15.750''"
    int fadeSide = 0;                 // Drag::fadeTension: 0 = the fade in, 1 = the fade out
    double fadeTensionStart = 0.0;    // its handle value (-1..1) when the drag started
    int fadeTensionStartY = 0;
    // warp markers (round 175a, M3; a LYNX extra, the user's choice: Live edits them only in its Clip View): an audio
    // clip whose channel has markers that bend the tempo shows them as small flags on its top edge, to drag sideways
    std::vector<WarpPoint> clipWarpMarkers (int clip) const; // as played (warpedit::playedMarkers); empty: none
    double clipFileSeconds (int clip) const;
    int warpMarkerAt (int clip, juce::Point<int> p) const; // the marker's index, or -1
    int warpMarkerNear (int clip, juce::Point<int> p) const;      // round 190gi: Alt + drag, anywhere on the body
    double warpStripSeconds (int clip, juce::Point<int> p) const; // round 190gi: a double-click on the flag strip
    int warpClip = -1, warpIndex = -1;
    double warpBeat = 0.0; // the dragged marker's beat (shown while dragging, applied on release)
    void updateScrollBars();
    bool isSelected (int index) const;
    void setSelection (std::vector<int> indices);
    // sorts while keeping the selection (no undo step); `placed` = the clips the edit put somewhere (moved, painted,
    // pasted…): their audio clips follow an audio track (issue #15, trackmodes::followAudioTracks)
    std::vector<int> sortClips (std::vector<int> placed = {}); // returns where the placed clips are now
    void askToMakeClipsUnique (std::vector<int> placed, std::vector<int> channels);
    void showClipMenu (int clip);
    void showRulerMenu (int x, int y = -1);               // markers, time signature and loop
public:
    // (round 190ol: the menu itself, also for the shots). Issue #281 (round 190qr): `y` is the press; on a marker's tag
    // (markerTagAt) the menu is that marker's, wherever on the tag, as FL's
    juce::PopupMenu rulerMenu (int x, int y = -1);
private:
    static juce::String markerLabel (const Marker& m);   // the text of a marker's tag on the ruler
    int markerTagAt (int x, int y) const;                 // the marker whose tag is under (x, y), or -1
    void editMarkerName (std::int64_t tick, int existing); // existing = index, or -1 to create
    void askSimpleSignature (std::int64_t bar);            // round 190ol (P6): the Simple ruler's time signature dialog
    // Round 190hq (B11, core/TempoMarkers.h): the tempo markers' tags on the ruler (LYNX extra): the one under (x, y), or
    // -1; the dialog that adds one at `tick` (existing −1) or edits marker `existing`; the tag being dragged
    int tempoTagAt (int x, int y) const;
    void editTempoMarker (std::int64_t tick, int existing);
    int tempoDragMarker = -1;
    std::int64_t tempoDragTick = 0;
    // FL 2026's Playlist › Time markers keys (round 167k): Alt+T, Ctrl+T, Shift+Alt+T, Shift+T
    std::int64_t songPositionTick() const;      // the playhead (song position), wrapped like centreOnPlayhead
    void addMarkerByKey();                      // "Add one..." at the time selection / visible playhead / first bar
    void addAutoMarker();                       // "Add auto" at the playhead, on the grid
    void addSignatureChangeByKey();             // "Add time signature change..." (dialog)
    void placeLoopMarker();                     // "Place loop" at the time selection / first bar in view
    // FL's Select › time keys (round 167k: EESelAroundSelAction Ctrl+Enter, EESelPrev/NextAction Ctrl+Left/Right)
    void selectTimeAroundSelection();
    void moveTimeSelection (int direction);
    // round 190on (P8, core/TransientNav.h): Tab to transient — the song position (`extend`: the time selection's edge) to the
    // next / previous attack of the selected audio clips (none selected: every audio clip)
    bool moveToTransient (int direction, bool extend);
    std::vector<double> clipAttacks (int index); // (file seconds, found once per sample)
    std::map<std::string, std::pair<std::weak_ptr<const SampleData>, std::vector<double>>> transientCache;
    void deleteSelection();
    void copySelection();
public:
    // round 190bv: the clipboard is shared with the clip grid (a clip copied in a slot pastes here and the other way);
    // the stamp tells the grid whether its own copy is still the newest
    static void setCopiedClips (std::vector<Clip> clips, trackclips::Carried patterns = {})
    {
        clipboard = std::move (clips);
        clipboardPatterns = std::move (patterns); // (round 190ne: the copied track clips, core/TrackClips.h)
        ++clipboardStamp;
    }
    static const std::vector<Clip>& copiedClips() noexcept { return clipboard; }
    static const trackclips::Carried& copiedPatterns() noexcept { return clipboardPatterns; }
    static std::uint64_t copiedStamp() noexcept { return clipboardStamp; }
    // issue #232 stage 2 (round 190nz, ui/WindowClipboard.h): the clipboard's clips, with what they play, on the system
    // clipboard for another LYNX SHARE window (the clip grid's copies too); and whether a paste has something (this
    // window's copy or another window's)
    static void publishCopiedClips (AudioEngine& engine);
    static bool canPasteClips();
    // round 190ld (--details190ldshot): the Simple header's Fx pill and its chain preset
    juce::Rectangle<int> trackFxForSnapshot (int track) const { return trackFx (track); }
    juce::String simpleFxPresetForSnapshot (int track) const { return simpleFxPreset (track); }

private:
    void pasteClips (std::int64_t at = -1);
    void normalizeClips (const std::vector<int>& targets, bool asGroup = false);
    juce::PopupMenu buildClipMenu (int index, int mouseX);
    void addKeyFollowItems (juce::PopupMenu& menu, int index, const std::vector<int>& targets); // round 190ds
    void addGrooveItems (juce::PopupMenu& menu, int index, const std::vector<int>& targets); // round 190fm: Playlist clip grooves

    // FL's audio clip menu (round 190dz, core/FlClipMenu.h): FL's two columns and sections, LYNX's own items after them
    juce::PopupMenu buildAudioClipMenu (int index, int mouseX);
    std::function<void (float, bool)> clipFitter (int index, std::shared_ptr<const SampleData> sample);
    juce::PopupMenu fitToTempoMenu (int index, std::shared_ptr<const SampleData> sample);
    void showDetectTempo (int index, std::shared_ptr<const SampleData> sample);
    std::shared_ptr<loopinfo::Shared> loopInfo; // round 190kx: the loop analysis (ui/LoopInfo), made when a clip menu first asks
    void duplicateSelection (bool linked = false); // linked: copies of track clips share their notes (round 190ne)
    void seekTo (int x, bool snapOn = true); // Alt held while scrubbing: snapOn false (free position, FL)
    void paintAudioClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    // Clips a few pixels wide (zoomed far out), gathered and filled per colour at the end (round 151)
    struct SmallClips;
    void addSmallClip (SmallClips& batch, float physicalScale, const Clip& clip, juce::Rectangle<float> bounds, bool current);
    void paintAutomationClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    // round 190jn (CL-17, CL-18): a clip's colour in Live's look (its own, else its track's) and its look; Live's lanes
    // (bar-group shading and grid); a clip's Live frame, returning its body (empty on a collapsed lane)
    juce::Colour liveClipColour (const Clip& clip) const;
    theme::ClipLook liveClipLook (const Clip& clip) const;
    void paintLiveGrid (juce::Graphics& g, juce::Rectangle<int> grid) const;
    // Round 190jn (the coordinator's follow-up): Live's lane backgrounds, grid and separators from one cached image, made
    // again only when the view's size, scroll, zoom, rows or bars change (false: draw them straight; whole-pixel scales
    // only). LYNX_LIVEGRID_CACHE=0 turns it off (checks).
    bool paintLiveLanesCached (juce::Graphics& g, juce::Rectangle<int> grid, int rows);
    juce::Image liveLaneImage;
    std::uint64_t liveLaneKey = 0;
    juce::Rectangle<float> paintLiveClipFrame (juce::Graphics& g, juce::Rectangle<float> r, const theme::ClipLook& look, const juce::String& name) const;
    // Live's grid lines from x0 to x1: level 0 = a bar group's first bar, 1 = a bar, 2 = a beat; `group` = the group's parity
    template <typename Line>
    void forEachLiveGridLine (double x0, double x1, Line&& line) const;

    void refreshArrangementBox();
    void arrangementChosen();
    void renameArrangement();
    // `alsoEdit` (round 190mp): run in the same edit (one undo step) after a non-empty name was given
    void renameTrack (int track, std::function<void (Song&)> alsoEdit = nullptr);
    void autoNameClipsFromMenu (int track, const std::vector<int>& tracks);
    void freezeTrackChannels (std::vector<std::uint64_t> uids); // round 190fo: the Simple track menu's Freeze
    void showTrackMenu (int track);
    juce::PopupMenu buildTrackMenu (int track);
    void openClipSource (int clip, bool other); // double click: the clip's pattern / automation (Alt: the other editor)
    juce::Rectangle<int> trackLed (int track) const;   // the mute LED on the right of a header (Simple headers: the M button)
    // the Simple headers' controls (round 184a): empty with FL's headers, a row too short or a track without an insert
    bool simpleHeaders = false;
    bool liveStyle = false;
    bool dragCopies = false; // the clips being moved are copies made at the press
    arrange::TimeClip timeClipboard;
    trackclips::Carried timePatterns; // round 190ne: the track clips of timeClipboard
    std::int64_t lastTrackClipLength = 0; // round 190ne: the length a new track clip is drawn with (0: one bar)
    // round 190ne: how many clips play each pattern while the song has track clips (the link mark on shared ones)
    int trackClipUseCount (int pattern) const;
    mutable std::vector<int> trackClipUses;
    mutable std::uint64_t trackClipUsesStamp = ~(std::uint64_t) 0;
    // the Simple headers' row and the height that shows their second row (round 190jc, SG-3: 64 / 54; Large controls 88 / 76)
    static int simpleTrackHeight();
    static int simpleTwoRows();
    // round 190jc (design190ia SG-2): the Simple layout paints its real tracks and the drop lane's row, nothing under
    // them (FL's 500 rows stay in the song); every other layout paints them all
    int paintedRows() const;
    juce::Rectangle<int> simpleAddButton() const; // the round "+" in the drop lane's header (empty: no drop lane)
    void paintSimpleHeaderControls (juce::Graphics& g, int track, juce::Rectangle<float> plateArea, bool hovered);
    bool dropLaneHot = false;
    void setDropLaneHot (bool hot);
    juce::Rectangle<int> dropLaneArea() const; // the lane in the grid (empty: none or not on screen)
    juce::Component dropLaneProbe; // its place for the first-song tour's ring ("simple.dropLane")
    // Round 190mk (DL-70, capture 190mf B1; core/InsertMark.h): Live's insert marker, a 1 px line over the clicked lane
    // pulsing cyan <-> white. Perf-first: an opaque child exactly the line's size, so a pulse repaints those 17 pixels and
    // nothing of the Arrangement's cached picture under it; its timer runs only while it is shown (Live's look, a marker
    // set, the line on screen). Placed from paint() (as dropLaneProbe), so scrolling and zooming move it.
    class InsertMarkLine final : public juce::Component, private juce::Timer
    {
    public:
        InsertMarkLine()
        {
            setOpaque (true);
            setInterceptsMouseClicks (false, false);
        }
        void restart()
        {
            startedMs = juce::Time::getMillisecondCounterHiRes();
            colour = insertmark::cyan;
            repaint();
        }
        std::uint32_t colourNow() const noexcept { return colour; }
        void setPhaseForSnapshot (double ms) { colour = insertmark::colourAt (ms); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (colour)); }
        void visibilityChanged() override
        {
            if (isVisible())
                startTimerHz (25);
            else
                stopTimer();
        }

    private:
        void timerCallback() override
        {
            if (! isShowing())
                return;
            if (const auto c = insertmark::colourAt (juce::Time::getMillisecondCounterHiRes() - startedMs); c != colour)
            {
                colour = c;
                repaint();
            }
        }
        double startedMs = 0.0;
        std::uint32_t colour = insertmark::cyan;
    };
    InsertMarkLine insertMarkLine;
    int insertMarkTrack = -1;
    std::int64_t insertMarkTick = -1;
    void setInsertMark (int track, std::int64_t tick);
    void updateInsertMarkLine();
    juce::String insertMarkText() const;
    void updateDropLaneProbe();
    juce::Rectangle<int> trackSolo (int track) const;
    juce::Rectangle<int> trackVolume (int track) const;
    juce::Rectangle<int> trackPan (int track) const;
    juce::Rectangle<int> trackFx (int track) const;
    juce::String simpleFxPreset (int track) const; // round 190ld (DS-13): the insert's chain preset ("" = none)
    int simpleFxWidth (int track) const;
    static juce::Font simpleFxFont();
    // issue #154 (round 190ht): the insert effects column of a linked header (empty: not shown on this track), the
    // width it takes, the row under a point (an effect index, trackinserts::addRow, -1), its painting and gestures
    juce::Rectangle<int> trackInserts (int track) const;
    int insertsColumn() const;
    int insertsItemAt (int track, juce::Point<int> p) const;
    int insertsFirst (int track) const; // the first row shown (the list's scroll)
    void paintInserts (juce::Graphics& g, int track, bool hovered);
    bool insertsMouseDown (const juce::MouseEvent& e);
    bool insertsAddSearch (juce::Point<int> p); // issue #206 follow-up: a middle-click on "+ Add" -> the effect search
    bool insertsMouseDrag (const juce::MouseEvent& e);
    bool insertsMouseUp (const juce::MouseEvent& e);
    bool insertsWheel (const juce::MouseEvent& e, float deltaY);
    bool insertsDrop (int track, juce::Point<int> p, EffectSlot fx); // a Browser effect dropped on the list
    EffectsPanel& insertActions (int insert); // the Mixer slot's own menus, windows and edits, on that insert
    std::unique_ptr<EffectsPanel> insertActionsPanel;
    std::map<int, int> insertsScroll; // insert -> first row shown (not saved)
    int insertsPressTrack = -1, insertsPressItem = -1, insertsDropTrack = -1, insertsDropAt = -1;
    bool insertsDragging = false;
    int simpleInsert (int track) const; // the insert a Simple (or linked) header's controls act on, -1 none
    // Round 190oo (design review 2, CS-24): an FL project's tracks in the Simple headers get S, volume, pan and "+FX" on
    // the insert their clips play through (the Session mixer's rule; the mute and solo rules: core/SimpleHeaderTarget.h).
    // headerInsert: the insert S / volume / pan / "+FX" act on (simpleInsert, or the borrowed insert in the Simple
    // headers), -1 none. The targets are built once per published song (the engine's publish count).
    int headerInsert (int track) const;
    const simplehdr::HeaderTarget& headerTarget (int track) const;
    juce::Rectangle<int> trackSharedMark (int track) const; // the "shared" mark left of M (empty: none)
    mutable std::vector<simplehdr::HeaderTarget> headerTargetList;
    mutable std::uint64_t headerTargetStamp = ~(std::uint64_t) 0;
    // LYNX's "Link Playlist tracks to mixer tracks" (round 190gz, issue #170): with FL's headers, the project's switch is
    // on. The headers are then wider and a linked track's header carries its mixer track's M, S, volume and pan (the
    // Simple headers' controls, laid out for FL's row heights: one row of buttons, the volume bar and pan from 34 px).
    bool linkedHeaders() const;
    void updateHeaderWidth(); // FL's, the Simple or the linked width, as the layout and the project ask
    static constexpr int linkedTwoRows = 34;
    void selectTrackChannel (int track); // the Simple workflow: a header click selects the track's channel
    int dropLaneTrack() const;          // the row the "drop files here" lane is drawn on, -1 none
    // a level drag on a Simple header: 1 volume, 2 pan (0 none)
    int levelDrag = 0, levelInsert = -1;
    float levelStart = 0.0f;
    int levelWidth = 1; // the volume bar's width at the press
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
    // issue #201 (round 190ll): a colour for several tracks; a Simple track's insert and channel follow it
    static void recolourTrack (Song& s, int track, std::uint32_t colour, bool simple);
    void colourTracks (const std::vector<int>& tracks, std::uint32_t colour);
    bool isTrackSelected (int track) const;
    std::vector<int> tracksForMenu (int track) const;
    bool trackStateMenus() const;                                  // round 190op (P4)
    void paintTrackStateMarks (juce::Graphics& g, juce::Rectangle<int> headers, int rows); // round 190op (P4)
    bool anyHiddenTracks = false; // round 190op (P4): the song holds hidden tracks (updateProjection)
    void addDeactivateTrackItem (juce::PopupMenu& menu, int track); // round 190op (P4)
public:
    // round 190pe (P20, LYNX extra; core/TrackVersions.h): the track's versions, as the track menu's submenu and the
    // header's version chip (a click opens the same menu). Nothing is drawn or looked up while no track has versions.
    juce::PopupMenu buildTrackVersionMenu (int track);
    juce::Rectangle<int> trackVersionChip (int track) const;
private:
    void paintVersionChips (juce::Graphics& g, juce::Rectangle<int> headers, int rows);
    void renameTrackVersion (int track, int version);
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
    std::int64_t rulerMark = -1; // issue #211 (round 190mq): the last single click on the ruler, FL's paste anchor; -1 = none
    juce::ModifierKeys downMods;
    bool startedOnEmpty = false, erasedSomething = false, holdExpired = false;
    std::vector<int> touchedClips;
    std::int64_t nextPaintTick = 0;
    Clip paintClip;
    juce::Point<int> knifeFrom, knifeTo;
    std::size_t knownClipCount = 0;
    static inline std::vector<Clip> clipboard;
    static inline trackclips::Carried clipboardPatterns; // round 190ne: the track clips the clipboard's clips play
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
    // Issue #207 (round 190lw, LYNX extras; core/AutoPointEdit.h). One curve's points selected together — an automation
    // clip's, an audio clip's envelope lane's or an automation lane's: the Select tool's rubber band
    // selects them; a drag on a selected point moves the group; Delete deletes them, Ctrl+C / Ctrl+X copy /
    // cut, Ctrl+V pastes at the mouse on a curve; Esc or a click elsewhere drops the selection. In the Clip-launch layout a
    // drag in an automation lane's background selects its points in that time range (Live). The pen: Alt held over an
    // automation clip's curve (Ctrl over an automation lane) shows a pencil, a drag draws freehand and the stroke is
    // thinned on release.
    struct CurveSpot
    {
        int clip = -1;             // a clip's curve, or
        int track = -1, lane = -1; // an automation lane's
        bool valid() const noexcept { return clip >= 0 || (track >= 0 && lane >= 0); }
        bool operator== (const CurveSpot&) const = default;
    };
    CurveSpot pointSpot;                     // the curve of the selection (and of a band / move / pen in progress)
    std::vector<int> selectedPoints;         // its selected points (ascending)
    std::vector<AutomationPoint> pointsBefore; // a group move / pen stroke: the curve when it started
    juce::Point<int> pointPress;
    juce::Rectangle<int> pointBand;          // the rubber band on screen (empty: none)
    bool penStarted = false, bandAdds = false, bandWholeHeight = false;
    std::int64_t penTick = 0, penLo = 0, penHi = 0;
    float penValue = 0.0f;
    static inline autopoints::Clipboard pointClipboard;
    CurveSpot curveSpotAt (juce::Point<int> p) const; // the editable curve whose area is under p (none: invalid)
    const std::vector<AutomationPoint>* spotPoints (const CurveSpot& s) const;
    juce::Rectangle<float> spotBody (const CurveSpot& s) const;
    void editSpot (const CurveSpot& s, const std::function<void (std::vector<AutomationPoint>&)>& fn);
    std::int64_t spotTickAt (const CurveSpot& s, float x) const;  // the curve's own tick under x (not snapped)
    std::int64_t spotSongTick (const CurveSpot& s, std::int64_t tick) const; // and back to the song's
    float spotValueAt (const CurveSpot& s, float y) const;        // the stored value under y (0..1)
    int spotPointAt (const CurveSpot& s, juce::Point<int> p) const;
    bool nearLaneLine (const CurveSpot& s, juce::Point<int> p) const; // within 5 px of an automation lane's line
    static bool penModifier (bool lane, juce::ModifierKeys mods);    // Alt on a clip's curve, Ctrl in a lane
    bool pointMouseDown (const juce::MouseEvent& e, int clipHit); // true: handled
    void pointMouseDrag (const juce::MouseEvent& e);
    void pointMouseUp (const juce::MouseEvent& e);
    bool pointKeyPressed (const juce::KeyPress& key);             // true: handled
    // round 190pa (research190oe P14, core/RangeEdit.h; PlaylistRange190pa.inc): a range on chosen tracks (REAPER's razor
    // edit). A plain drag on empty space draws it in the Clip-launch Arrangement (Live's time selection, rollup 0.1.31 step 7);
    // Ctrl+Alt+drag in the Pattern layout with Settings › LYNX extras "Range edits on chosen tracks"; a drag inside it moves its content, Delete / Ctrl+X / C / V / B / D / E act on it,
    // a right-click opens its menu, Esc or a press elsewhere clears it. Song::trackRange holds it.
public:
    enum class RangeCommand { cut, copy, paste, clear, duplicate, split, render, deselect };
    bool rangeEditsOn() const;
    void rangeCommand (RangeCommand which);
    void setTrackRangeForSnapshot (const TrackRange& r);
    juce::PopupMenu rangeMenuForSnapshot() { return makeRangeMenu(); }
    // the drawn range at a corner pair as the gesture leaves it (tests / shots: Ctrl+Alt+drag from `a` to `b`)
    void drawRangeForSnapshot (juce::Point<int> a, juce::Point<int> b);
private:
    bool rangeMouseDown (const juce::MouseEvent& e);
    void rangeMouseDrag (const juce::MouseEvent& e);
    void rangeMouseUp (const juce::MouseEvent& e);
    bool rangeKeyPressed (const juce::KeyPress& key);
    void paintTrackRange (juce::Graphics& g);
    juce::PopupMenu makeRangeMenu();
    std::vector<juce::Rectangle<int>> rangeRects (const TrackRange& r) const;
    bool rangeHit (juce::Point<int> p) const;
    void setTrackRange (const TrackRange& r);
    TrackRange rangeAt (juce::Point<int> a, juce::Point<int> b) const;
    TrackRange rangeDraft;
    juce::Point<int> rangeDownPos;
    // (rollup 0.1.31 step 7) the Clip-launch Arrangement's plain drag on empty space: its press, replayed when it was a click
    bool rangePlainPress = false, rangeReplaying = false;
    std::optional<juce::MouseEvent> rangePress;
    static rangeedit::RangeClip& rangeClipboard();
    static bool& rangeClipboardNewest();
    static int& rangeClipboardTop();
    void clearPointSelection();
    bool isPointSelected (const CurveSpot& s, int point) const;
    void paintPointSelection (juce::Graphics& g);                 // the selected points' rings and the rubber band
    bool makePointDragHint (ui::Hint& hint);
    // FL 2026's audio clip gain handle (round 166e): a semicircle at the bottom centre of the clip; a vertical drag
    // changes the gain of the clip (or of every selected audio clip, each from its own value) by 0.05 dB per pixel
    juce::Rectangle<float> gainHandle (int clip) const; // empty: the clip has none (not audio, too small)
    // round 190jr (BUILD-LIST 13): the Simple layout's region loop handle — BandLab's circular arrow at the right end of a
    // region's title strip; a drag right repeats the region (core/RegionLoop.h). Empty: none (not Simple, not loopable,
    // too narrow, or a repeat that another repeat follows).
    juce::Rectangle<float> regionLoopHandle (int clip) const;
    bool regionLoopContinues (int clip) const; // a repeat of the same run starts where `clip` ends
    void regionLoopDragTo (int x);
    int loopTrack = -1;                 // the run being dragged: its region's track, start and source
    std::int64_t loopStart = 0;
    Clip loopLike;
    // the Simple automation view (setSimpleAutomation)
    bool simpleAutomation = false;
    std::map<int, AutomationTarget> simpleAutoTargets; // per track: the parameter its row shows (none: the insert's volume)
    bool simpleAutoOn (int track) const;                // the view is on and `track` is a Simple track
    AutomationTarget simpleAutoTarget (int track) const;
    int simpleAutoLane (int track) const;              // the lane of that target on the track, or -1
    float simpleAutoCurrent (const AutomationTarget& target) const; // the target's value now (0..1), for the flat line
    juce::Rectangle<int> simpleAutoMenu (int track) const; // the header's parameter menu (in place of volume, pan, FX)
    void showSimpleAutoMenu (int track);
    juce::PopupMenu buildSimpleAutoMenu (int track);
    juce::PopupMenu withRegionLoopItems (juce::PopupMenu menu, int clip); // the Simple layout's Loop until next / Unloop (at the end)
    bool simpleAutoMouseDown (const juce::MouseEvent& e); // true: handled
    void paintSimpleAutomation (juce::Graphics& g, juce::Rectangle<int> grid);
    void showGainMenu (int clip);
    std::vector<double> gainStartDb; // per dragIndices, when the gain drag started
    int gainStartY = 0;
    static inline double copiedGainDb = 0.0;
    static inline bool hasCopiedGain = false;
    static inline double copiedFadeMs = 0.0; // the fade handle menu's Copy value (round 189i)
    static inline bool hasCopiedFade = false;
    static inline double copiedFadeTension = 0.0; // the tension handle menu's Copy value (round 190jz)
    static inline bool hasCopiedFadeTension = false;
    // FL: a double click held on the ruler drags a time selection (as Ctrl+drag); released without moving it clears
    bool rulerDoubleDrag = false;
    bool rulerMenuOnClick = false; // a right press on the ruler: the ruler menu if released in place (round 168t)
    juce::Rectangle<float> automationBody (const Clip& clip) const;
    // round 190gi: the curve a clip edits in place — an automation clip's points, or an audio clip's envelope lane shown
    // on it (core/ClipEnvelope.h); the point and handle helpers below work on either. editCurve: one undoable edit (the
    // audio clip gets its own envelope first).
    struct CurveRef
    {
        const std::vector<AutomationPoint>* points = nullptr;
        int automation = -1;   // an automation clip's automation
        std::uint8_t lane = 0; // an audio clip's envelope lane (clipenv::volumeLane / panLane)
    };
    CurveRef curveOf (int clip) const;
    void editCurve (int clip, const std::function<void (std::vector<AutomationPoint>&)>& fn);
    float curveValueAt (int clip, double clipTick) const;
    juce::String curveValueText (int clip, float value) const;
    static juce::String tensionText (float tension); // round 190im: "Tension: 40%" (FL's sign)
    void paintClipEnvelope (juce::Graphics& g, const Clip& c, const ClipEnvelope& envelope);
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
    // Round 190ef: in the Clip-launch layout, the clip being moved dragged off the Playlist over the clip grid: the clips go
    // back where they were and the grabbed one becomes a drag the grid takes (a copy into a slot). True: done.
    bool dragOutToClipGrid (const juce::MouseEvent& e);
    void showLaneMenu (int track, int lane);
    void showAddLaneMenu (int track);
    void toggleAutomationLanes();
    bool laneButtonHover = false;
    int laneTrack = -1, laneIndex = -1;                    // the lane a lanePoint / laneTension drag edits
    float tensionStart = 0.0f, tensionSign = 1.0f;
    // round 190ef, Draw Mode (core/DrawMode.h): a laneDraw drag's last mouse position (tick, value) and whether it draws
    // freehand (no grid, or Alt) or steps at the grid width
    std::int64_t drawTick = 0;
    float drawValue = 0.0f;
    bool drawFreehand = false;
    void laneDrawTo (const juce::MouseEvent& e);
    // hover: what a click would hit (drives the cursor, the highlight and the hint)
    struct Hover
    {
        int clip = -1, track = -1;
        gesture::Target target = gesture::Target::empty;
        bool header = false, led = false, arrow = false, arm = false, input = false, audition = false;
        bool solo = false, volume = false, pan = false, fx = false; // the Simple headers' controls (round 184a)
        bool addTrack = false; // round 190jc: the round "+" under the Simple tracks
        bool shared = false;   // round 190oo (CS-24): the Simple header's "shared" mark
        int divider = -1; // the track whose bottom edge in the headers is under the mouse (resizes it)
        int insertItem = -1; // issue #154: the insert effects column's row (an effect, trackinserts::addRow; -1 none)
        bool insertLed = false; // ... and its on/off dot
        int fade = 0; // round 190jz: the fade handle (1 in, 2 out) or tension circle (3 in, 4 out) under the mouse
        bool operator== (const Hover& o) const
        {
            return fade == o.fade && clip == o.clip && track == o.track && target == o.target && header == o.header && led == o.led && arrow == o.arrow
                   && arm == o.arm && input == o.input && audition == o.audition && solo == o.solo && volume == o.volume && pan == o.pan && fx == o.fx
                   && divider == o.divider && insertItem == o.insertItem && insertLed == o.insertLed && addTrack == o.addTrack
                   && shared == o.shared;
        }
    };
    Hover hover;
    juce::Point<int> lastMouse { -1, -1 };
    Hover hoverAt (juce::Point<int> p) const;
    void updateHover (juce::Point<int> p, juce::ModifierKeys mods);
    void updateCursor (juce::ModifierKeys mods);
    juce::Rectangle<int> clipBounds (int clip) const;
    void postDragHint (); // what the gesture in progress does (hint bar)
    bool makeDragHint (ui::Hint& hint); // (round 190im: the hint; false = none for this drag)
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
    ui::OnDemandVBlank vblank { *this, [this] { glideFrame(); return gliding; } }; // (attached only while gliding)
    ui::FrameTicker frames { *this, [this] (double ms) { return liveFrame (ms); } };
    // round 190w: the track headers' level meters (FL's "Show levels on audio / instrument tracks")
    int meteredInsertOf (int track) const;
    bool updateMeterColumn();        // true when the column appeared or went away
    bool stepHeaderMeters (double nowMs);
    int meterColumn = 0;             // px at the right of the headers for the meters (0 = no track shows one)
    // the column at the headers' right edge: FL's selection strip (9 px, live check 190gu B1; the meters sit in it), or
    // in the Simple headers the meters' column alone
    int rightColumn() const noexcept { return simpleHeaders ? meterColumn : trackhdr::selectionStripWidth; }
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
    // The Sections lane (round 190ol)
    friend class SectionsLaneView;
    std::unique_ptr<SectionsLaneView> sectionsLane;
    bool simpleSectionsHidden = false; // the Simple "Sections" button hid a lane that has sections (view only)
    int laidOutSectionsHeight = 0;
    // The Chord lane (round 190pf)
    friend class ChordLaneView;
    std::unique_ptr<ChordLaneView> chordLane;
    int laidOutChordHeight = 0;

    // issue #250 (round 190oq): deleted clips fade out as FL's do (ui/DeleteFade.h); `fadeOut` before they go
    ui::DeleteFade deleteFade { *this, ui::DeleteFade::playlistMs };
    void fadeOut (const std::vector<int>& clips);

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
    void mouseDrag (const juce::MouseEvent& e) override;
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

    // round 190im (R5, DC-3): the patterns playing now, marked as FL marks them (a light-blue bar at the entry's right
    // edge, shots/fl-picker-stopped-vs-playing.png); only the entries that change are repainted
    void setPlayingPatterns (std::vector<int> now);

    // Round 190jj (C-11): the drag image of list row `listRow` (the entry, semi-transparent) and where it sits from the
    // mouse (FL 2026: below and to the right); --capfix190jjshot draws it as the drag would
    juce::Image dragImageOf (int listRow);
    static juce::Point<int> dragImageOffset() noexcept { return { -34, 40 }; }
    int listRowOfPattern (int pattern) const;
    const std::vector<int>& playingForCheck() const noexcept { return playingPatterns; }
    int rowCentreForSnapshot (int pattern) const; // (round 190mp: the y of a pattern's row, -1 = not listed)
    // round 190ne: the track clips' group (folded by default) and its row's "shown" switch, for --trackclips190neshot
    void setTrackClipsOpen (bool open) { trackClipsOpen = open; repaint(); }
    bool trackClipsOpenNow() const noexcept { return trackClipsOpen; }
    int groupRowForSnapshot() const
    {
        const auto& list = rows();
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(std::size_t) i].group)
                return i;
        return -1;
    }
    juce::Rectangle<int> groupSwitchForSnapshot (int listRow) const { return groupSwitchBounds (listRow); }
    int listedRowsForSnapshot() const { return (int) rows().size(); }
    int rowYForSnapshot (int listRow) const { return headerHeight + (listRow - scroll) * rowHeight + rowHeight / 2; }

    // the kind listed: 0 = patterns, 1 = audio clips, 2 = automation clips
    void setView (int newView);
    int getView() const noexcept { return view; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Row
    {
        ClipKind kind;
        int index;            // pattern, audio or automation channel (-1: the track clips' group row)
        juce::String name;
        juce::Colour colour;
        bool group = false;   // round 190ne: the "Track clips (N)" group row (folded by default)
        bool trackClip = false; // round 190ne: a track clip, listed under the group
        int uses = 0;         // round 190ne: a track clip: how many clips play it (shared = linked copies)
    };
    // The list (cached per published song, view, group state and clip look: round 190ne, the Picker repaints often)
    const std::vector<Row>& rows() const;
    mutable std::vector<Row> rowCache;
    mutable std::uint64_t rowCacheStamp = ~(std::uint64_t) 0;
    mutable int rowCacheKey = -1;
    bool trackClipsOpen = false; // round 190ne: the group is unfolded
    juce::Rectangle<int> groupSwitchBounds (int listRow) const; // round 190ne: the group row's "shown" switch
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
    // FL's Alt+right-hold preview (round 190mp, DE-134; capture 190mp A6): while held the pattern plays in pattern mode;
    // the release stops it and puts back the mode it had (-1 = no preview running)
    int previewModeBefore = -1;
    int pressedRow = -1;
    int dragRow = -1;      // the row a left press landed on (issue #177: a drag from it goes to the Playlist)
    bool dragging = false; // that drag has started
    int hoverTab = -1, hoverRow = -1;
    bool hoverAdd = false;
    std::vector<int> playingPatterns; // round 190im: sorted
    friend class PlaylistComponent;
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
    GridView getGridView() const noexcept { return gridView; } // (round 190iv: the Live control bar's view selector)
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
    juce::Component* getOverlay() const noexcept { return overlay; }
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
// Round 190jn (design190ia CL-17, owner decision): View › "Tall Arrangement lanes" in the Clip-launch layout's Live look
// (a preference, off by default: Live's 19 px collapsed lanes; on: LYNX's 38 px rows). PlaylistComponent::liveLanesChanged
// after a change.
bool& playlistTallLiveLanes();
} // namespace lynx
