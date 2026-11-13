#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MeterBallistics.h"
#include "core/MixerLayout.h"
#include "core/MixerOps.h"
#include "engine/AudioEngine.h"
#include "ui/TimerWhileShowing.h"
#include "ui/FrameTicker.h"
#include "ui/ValueControl.h"

namespace lynx
{
// The built-in effects offered in the slot menus and the quick-add palette, in menu order.
const std::vector<EffectKind>& addableEffects();
// The FL engines among them need the user's FL installation (core/NativeEngines.h, round 142): whether one can be
// added here, its menu name ("(beta)", "(requires FL Studio)") and a new slot (nullopt = not available here)
bool addableEffectAvailable (EffectKind kind);
// One of FL Studio's own effects (by its FL name, even when LYNX made it): the plugin menus file it under FL's category
// (round 170b); LYNX's own effects go under "LYNX SHARE".
bool isFlEffect (EffectKind kind);
juce::String addableEffectName (EffectKind kind);
std::optional<EffectSlot> newAddableEffect (EffectKind kind, double bpm = 120.0);

// FL's mixer track state (Mixer track menu › File, REA round 143), in LYNX's .lynxtrack format: load onto `track`
// (or, with `asNewInsert`, onto a new insert after it, named after the state), save `track`'s state.
bool loadMixerTrackState (AudioEngine& engine, int track, const juce::File& file, bool asNewInsert);
bool saveMixerTrackState (AudioEngine& engine, int track, const juce::File& file);

// FL's mixer presets (REA round 167q): FL's own (.fst of Data\Patches\Mixer presets and Documents\Image-Line\FL
// Studio\Presets\Mixer presets) and LYNX's states (.lynxtrack of Documents\LYNX\Mixer states), all read here.
namespace mixerpresets
{
// The folders FL lists (user's first, then FL's factory one) and LYNX's states folder; only the existing ones.
juce::Array<juce::File> folders();
// Every preset of these folders (subfolders too), sorted by name as FL's File › States list.
juce::Array<juce::File> all();
bool isMixerPreset (const juce::File& file); // .lynxtrack, or an .fst holding a mixer track
// The preset as a track state (FL's: converted, name "" when it has none); nullopt if unreadable.
std::optional<MixerTrack> read (const juce::File& file);
// FL's load (FUN_012a7440): onto every track of `tracks` (one undo step); routing, input, monitoring and arm stay.
bool loadOnTracks (AudioEngine& engine, const std::vector<int>& tracks, const juce::File& file);
// FL's "Insert from state" (a new insert at `at`) and "Add from state" (`at` = the end): the new track, or -1.
int insertTrack (AudioEngine& engine, int at, const juce::File& file);
} // namespace mixerpresets

// External audio input of the inserts (REA_rodada88): FL's "Audio input source" of a mixer track.
namespace mixerinput
{
// Opens Settings > Audio (the main window sets it).
extern std::function<void()> openAudioSettings;
// The insert's input menu: (none), the open device's stereo pairs and single channels, the monitoring mode and
// "Audio settings...". Opens the device input first when it is closed. `chosen` runs after a source is picked.
void showMenu (AudioEngine& engine, int track, juce::Component* target, std::function<void()> chosen = {});
// The arm switch: disarms, or arms at once — also an insert without an input source, as FL 2026 does.
void toggleArm (AudioEngine& engine, int track, juce::Component* target);
// FL's right-click on the arm switch (round 167a [SHOT s20–s27]): a Save dialog "Record <insert> to" (the recordings
// folder, "<project>_<date time>_<insert>.wav"); the file chosen becomes the insert's record-to file and arms it.
void chooseRecordTo (AudioEngine& engine, int track);
// The arm switch's hint: FL's "Record to: <file>" when the insert has one, then what the switch does.
juce::String armHint (const AudioEngine& engine, int track);
// Explains a recording problem to the user, with a way to Settings > Audio when it is about the device.
void explain (const AudioEngine::RecordingNotice& notice);
} // namespace mixerinput

// Hardware output targets (REA round 156; LYNX extra): an insert can also go straight to device outputs, a stereo
// pair or one channel alone (mono: sum / left / right). Optional: by default inserts reach the device via the Master.
namespace mixeroutput
{
// "(none)", "Out 1 - Out 2", "Out 3 (mono)", "... (missing)" — translated. `compact`: the strip badge ("→ 3 m").
juce::String label (const AudioEngine& engine, int track, bool compact = false);
// Whether the track has a target other than its default (inserts: any target; Master: not the first pair).
bool isCustom (const AudioEngine& engine, int track);
// The output target menu of a track.
void showMenu (AudioEngine& engine, int track, juce::Component* target, juce::Rectangle<int> screenArea);
} // namespace mixeroutput

// Mixer options (round 163): Options › Mixer and the mixer track menu's "Mixer" section.
namespace mixeroptions
{
// Called after an option kept in ui-options.txt changed (the main window saves the file).
extern std::function<void()> changed;
// Shows or hides the unused inserts in the mixer (the main window points it at its mixer).
extern std::function<void (bool)> setShowUsedOnly;
// Shows or hides the loudness panel beside the Master and starts or stops its measurement (round 190am).
extern std::function<void (bool)> setShowLoudness;
// Opens the Export window for these inserts only (FL's "Render armed / selected tracks to wave file(s)"; the main
// window sets it, with the project's folder and name). `done` (optional) runs once the files are written.
extern std::function<void (std::vector<int>, std::function<void (const std::vector<juce::File>&)> done)> renderTracks;
// Selects these Playlist tracks and scrolls the first into view (FL's "Select › Channels and playlist tracks routed to
// this track", round 190fl; the main window points it at its Playlist).
extern std::function<void (std::vector<int>)> selectPlaylistTracks;
// The items: this project's clean mixer, show only used inserts, remove unused inserts at the end and — with
// `newProjects` — the new-project choices (inserts, Master chain, clean mixer, Playlist tracks).
void addItems (juce::PopupMenu& menu, AudioEngine& engine, bool newProjects);
} // namespace mixeroptions

// FL-style mixer: one compact vertical strip per track (Master first).
//  - header: record arm, number, name (once), effect count; drag it sideways to reorder inserts
//  - pan, stereo separation and fader: the shared control contract (ui/ValueControl.h): drag
//    (Ctrl: fine), wheel, Alt/middle-click or double-click resets, right-click: type value / reset /
//    automation; the value shows next to the mouse while dragging
//  - green LED = mute (click), "S" = solo; stereo peak meters with peak hold and a clip latch
//    (click the meter to clear it)
//  - clicking a strip selects it; right-click opens the menu (rename, route the selected
//    inserts to this track); "+" at the end adds an insert
//  - routing lane at the bottom: with an insert selected, every other strip shows a send switch
//    (click toggles the send from the selected insert; right-click: send only / sidechain), a send
//    level knob when the send is on, and wires show where the selected insert goes (red) and what
//    feeds it (blue)
class MixerStrips final : public juce::Component,
                          public ui::HintSource,
                          public juce::DragAndDropTarget,
                          private juce::ChangeListener
{
public:
    explicit MixerStrips (AudioEngine& engine);
    ~MixerStrips() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    void updateSize();
    void setVisibleHeight (int height);

    int getSelectedTrack() const noexcept { return selected; }
    // The level a track's meter shows now, 0..1 (side 0 left, 1 right; round 183a: the Device View reads it while the
    // strips are on screen instead of taking the peaks they need).
    float shownLevel (int track, int side) const noexcept
    {
        const auto i = (std::size_t) (track * 2 + side);
        return track >= 0 && (side == 0 || side == 1) && i < levels.size() ? levels[i].level : 0.0f;
    }
    void selectTrack (int track);
    std::function<void (int)> onSelectionChanged;

    // An effect dragged from the Browser's Plugin database onto a strip (round 162): it goes into that insert.
    std::function<void (int, EffectSlot)> onEffectDropped;
    // A generator dragged onto a strip: a new channel routed to that insert.
    std::function<void (int, ChannelDef)> onInstrumentDropped;
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    // Multi-selection (docs/WORKFLOW_FL.md P2, M1; FL's own, round 170b): Ctrl+click or Ctrl+drag across strips selects
    // a range, Ctrl+Shift adds or removes it, Shift+click selects a range (LYNX); selected faders and pans move together
    // and the route arrow routes them all. Always contains the selected track.
    const std::vector<int>& getSelection() const noexcept { return selection; }
    void setSelection (std::vector<int> tracks, int primary);
    bool moveSelected (int delta);           // Alt+Left/Right
    void linkSelectedChannels (bool startingHere); // Ctrl+L / Ctrl+Shift+L
    void selectLinkedChannels();             // Alt+L
    // FL's "Delete" (round 186c): the confirmation "Delete tracks" (with "Remember my choice"), then, when a Playlist
    // audio / instrument track is linked to one of them, "Delete tracks and channels" / "Unlink tracks and channels"
    void deleteTracks (std::vector<int> tracks);
    void deleteSelectedTracks() { deleteTracks (selection); }
    // The insert's right-click menu (FL's mixer track menu, REA rounds 115/134/143); public for --snapshot and tests.
    juce::PopupMenu buildMenu (int track);
    juce::PopupMenu buildSendMenu (int track); // the route arrow / send knob menu from the selected track to `track`
    // runs the selected track's menu item whose caption (before any "\t" key text) is `caption` (translated): the
    // Mixer's menu keys (round 167k: FL's F2, S, Alt+R, Shift+Alt+R) do exactly what the menu does. False when absent
    // or disabled.
    bool runMenuItem (const juce::String& caption);

    // Mixer menu › "Show only used inserts" (round 163; ui-options.txt "mixerUsedOnly"): inserts nothing uses
    // (Song::insertInUse) take no room; "+" brings back the first unused insert before adding one.
    // Round 190am: the selected inserts and any insert whose meter moves always show (mixerops::hiddenUnusedInserts).
    static inline bool showUsedOnly = false;
    // Mixer menu › "Loudness meter beside the Master (LYNX)" (round 190am; ui-options.txt "mixerLoudness"; off = FL's
    // mixer, and nothing is measured): a narrow panel right of the Master with the short-term and integrated loudness
    // (LUFS) and the true peak (dBTP) of the Master's output, and a Reset button (core/MasterLoudness.h).
    static inline bool showLoudness = false;
    void setShowLoudness (bool on);
    void resetLoudness();
    void refreshLoudnessForSnapshot() { loudnessShown = engine.getMeters().loudness.read(); repaint(); } // (--snapshot: no frames run)
    juce::Rectangle<int> loudnessArea() const noexcept; // the panel's plate; empty while it is off
    // Mixer menu › "Keep links to deleted inserts, greyed (LYNX)" (round 186c, pain point 1; ui-options.txt
    // "mixerKeepLostLinks"; off = FL): Delete keeps each send into a deleted insert as a greyed MixerTrack::lostSends link.
    static inline bool keepLostLinks = false;
    // Mixer menu › "Mixer groups follow the Playlist (LYNX)" (round 186c, pain point 4; ui-options.txt
    // "mixerFollowPlaylist"; off by default): mixerops::followPlaylistGroups runs after every edit that changes them.
    static inline bool followPlaylist = false;
    bool isHidden (int track) const noexcept { return track > 0 && track < (int) hidden.size() && hidden[(std::size_t) track]; }

    // FL's "Mixer layout" (issue #52, REA round 168h; ui-options.txt "mixerLayout", per user like FL's FXViewMode):
    // the strip widths and arrangement of every mixer view (core/MixerLayout.h).
    static inline mixerlayout::Layout layout = mixerlayout::defaultLayout;
    static const mixerlayout::Spec& spec() noexcept;
    void setLayout (mixerlayout::Layout newLayout);
    std::function<void()> onLayoutChanged; // the main window saves ui-options.txt
    int widthOf (int track) const noexcept { return stripWidthOf (track); }
    // x of a strip (`track` == the number of tracks: the "+" button), after the docks and separators (round 146a)
    int stripLeft (int track) const noexcept { return stripX (track); }

    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    // --uiperf (round 173f): where a fader drag starts (the fader's rail, strip coordinates)
    juce::Rectangle<int> faderRailForSnapshot (int track) const { return faderRail (track); }
    void stepHeldRail(); // one step of the rail press being held (also called by --selftest, without the timer)
    // --uiperf: how many repaints spoiled the strips' picture and how often it was drawn again, since the last call
    std::pair<int, int> cacheCountersForPerf() noexcept { return { std::exchange (pictureInvalidations, 0), std::exchange (pictureRefills, 0) }; }

private:
    enum class Part { none, pan, stereo, fader, meter, mute, solo, route, send, header, body, add, arm, input, fold, tab };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    int stripX (int track) const noexcept;
    // FL's docks and separators (REA round 146a): the strips' x in display order (left block, middle, right), the
    // x of each gap between two dock blocks and of each separator line
    void layoutStrips();
    std::vector<int> stripXs;                                    // layout x (before pinning)
    std::vector<bool> hidden;                                    // per track: not shown (unused, "Show only used inserts")
    static constexpr int hiddenX = -100000;
    std::set<int> revealed;                                      // unused inserts shown anyway (added with "+", selected, or receiving audio)
    void keepSelectionShown();                                   // "Show only used inserts": the selection never hides
    // the loudness panel (round 190am): its layout x (hiddenX while off), what it shows now and its hover state
    static constexpr int loudnessWidth = 58;
    int loudnessX = hiddenX;
    MasterLoudness::Reading loudnessShown;
    bool loudnessResetHovered = false;
    juce::Rectangle<int> loudnessResetArea() const noexcept;
    juce::Rectangle<int> loudnessBarArea() const noexcept;
    void paintLoudnessPanel (juce::Graphics& g) const;  // the plate, captions and scale (kept in the picture)
    void paintLoudnessValues (juce::Graphics& g) const; // the numbers and the bar (drawn over the picture)
    void showLoudnessMenu();
public:
    void setShowUsedOnly (bool on)
    {
        showUsedOnly = on;
        revealed.clear();
        updateSize();
        repaint();
    }
private:
    std::vector<std::pair<int, MixerDock>> dockGaps, separatorLines;
    int blockStart[3] {}, blockEnd[3] {};                        // each dock block's layout span (0 width = empty)
    int contentWidth = 0;
    // FL keeps the left / right docks in view while the middle scrolls (round 149): the x shift of a dock block
    // for the viewport's current scroll position (0 for the middle block)
    int pinOffset (MixerDock dock) const noexcept;
    MixerDock dockOf (int track) const noexcept;
    void moved() override { repaint(); }
    static constexpr int dockGap = 14, separatorGap = 8;
    int stripWidthOf (int track) const noexcept
    {
        return track == 0 ? spec().masterWidth : isFoldTab (track) ? foldTabWidth : spec().stripWidth;
    }
    // Folded mixer groups (round 177a): how each strip shows (mixerops::foldView), the tab each hidden member's meter
    // feeds (-1 = none), and the fold chevron of a group's first strip. All empty / unused while no group is folded.
    std::vector<mixerops::FoldShow> foldShow;
    std::vector<int> tabOf;
    std::vector<bool> foldHead;
    static constexpr int foldTabWidth = 20;
    bool isFoldTab (int track) const noexcept { return track > 0 && track < (int) foldShow.size() && foldShow[(std::size_t) track] == mixerops::FoldShow::tab; }
    juce::Rectangle<int> foldArea (int track) const;
    float tabLevel (int track) const noexcept;
    void paintFoldTab (juce::Graphics& g, int track);
    void paintFoldTabLevel (juce::Graphics& g, int track) const;
    // VCA faders (round 177a): their own block left of the Master. A VCA strip goes by a pseudo track index
    // (vcaStrip (i) = -2 - i), so the strips' area helpers place its fader, mute and solo. All empty without a VCA.
    std::vector<int> vcaXs;
    static constexpr int vcaStrip (int i) noexcept { return -2 - i; }
    int vcaAt (int x) const noexcept;
    Part vcaPartAt (int i, juce::Point<int> p) const;
    float vcaMeter (int i, int channel) const noexcept; // the loudest member's level
    float vcaShownLevel (int i) const noexcept;         // the VCA's fader (its automation's value while it plays)
    ui::ValueSpec vcaSpec (int i) const;
    void setVcaLevel (int i, double value);
    void paintVcaStrip (juce::Graphics& g, int i);
    void paintVcaMeter (juce::Graphics& g, int i) const;
    void vcaMouseDown (const juce::MouseEvent& e, int i);
    void showVcaMenu (int i);
    void soloVca (int i);
    ui::Hint vcaHint (int i, juce::Point<int> p) const;
    void repaintVca (int i);
    juce::String vcaNote (int track) const; // a member's fader hint: the VCAs and the level that plays
    int dragVca = -1;
    std::vector<float> vcaLevelsShown, vcaMetersShown;
    // the strip's rows for the current layout and height (top to bottom)
    struct Rows
    {
        int numberY = 4, iconY = 19, header = 38;        // number, icon, end of the header (with a horizontal name)
        int nameTop = 0, nameHeight = 0;                // the vertical name column (alt layouts: low in the strip)
        int knobsTop = 0, knobsHeight = 0, buttonsTop = 0, buttonsHeight = 0;
        int faderTop = 0, faderBottom = 0, readoutHeight = 0;
        int armY = -1;                                  // compact layouts: the arm button's row under the fader
    };
    Rows rows() const noexcept;
    juce::Rectangle<int> nameArea (int track) const;
    juce::Rectangle<int> iconArea (int track) const;
    int trackAt (int x) const noexcept;
    Part partAt (int track, juce::Point<int> p) const;
    juce::Rectangle<int> faderArea (int track) const;
    float shownVolume (int track) const; // the fader's value, or its automation's while the song plays (issue #12)
    void repaintAutomatedStrips();
    // the meters, input bars and automated controls, at the display's refresh (round 174e; was a 60 / 30 Hz timer)
    bool liveFrame (double nowMs);
    // The strips kept as a picture of the visible part, with empty meter wells; the levels are drawn over it (round
    // 174e2): a meter frame blits its rows and draws the bars instead of painting the strips under them again. Other
    // repaints (a fader, a name, hover) refresh their area of the picture. LYNX_MIXER_CACHE=0: off.
    class MeterCache;
    bool meterCacheOn = false;
    bool meterRepaintOnly = false; // set around the meters' own repaints: the picture stays valid
    bool cachePainting = false;    // paint() is filling the picture: wells without levels
    int pictureInvalidations = 0, pictureRefills = 0; // what --uiperf reports (see cacheCountersForPerf)
    void paintMeterLevel (juce::Graphics& g, int track, int channel, juce::Rectangle<int> bar) const;
    bool isSilenced (int track) const; // muted, or silenced by a solo / a muted VCA (issue #144)
    void paintMeterLevels (juce::Graphics& g) const; // every meter in the clip, over the picture
    juce::Rectangle<int> nameBars (int track) const; // the thin level bars beside a vertical name (Wide layouts)
    void paintNameBars (juce::Graphics& g, int track) const;
    std::vector<int> sendAutomatedStrips;           // strips whose route knob shows an automated send
    std::uint32_t shownAutomation = 0;
    double lastFrameMs = 0.0;
    std::vector<std::array<float, 2>> automatedShown; // the volume and pan drawn per strip while automated (round 174e)
    std::uint64_t seenPublish = 0;                     // AudioEngine::getPublishCount at the last full refresh
    std::vector<std::array<float, 3>> shownLevels;   // volume, pan, separation per strip then (round 171c)
    std::vector<int> automatedStrips;
    juce::Rectangle<int> panArea (int track) const;
    juce::Rectangle<int> muteArea (int track) const;
    juce::Rectangle<int> soloArea (int track) const;
    juce::Rectangle<int> routeArea (int track) const;
    juce::Rectangle<int> armArea (int track) const;
    juce::Rectangle<int> inputArea (int track) const; // the input level bar (inserts with an input source)
    juce::Rectangle<int> stereoArea (int track) const;
    juce::Rectangle<int> meterArea (int track) const;
    // round 177d: the gain-reduction meter beside the peak meters (only on strips with one of LYNX's own dynamics)
    juce::Rectangle<int> grArea (int track) const;
    float gainReduction (int track) const; // 0..1 over mixerops::gainReductionRangeDb, -1: no such effect on the strip
    juce::Rectangle<int> sendArea (int track) const;   // send level knob (routing lane)
    juce::Rectangle<int> headerArea (int track) const;
    juce::Rectangle<int> faderRail (int track) const;  // the fader without the meters
    int laneTop() const noexcept;
    void paintRouting (juce::Graphics& g);
    void showTip (Part part, int track, bool transient);
    // a press on a fader's rail (FL: steps towards the mouse while held, REA_rodada177a section 4.2): the insert or VCA strip
    // held (railNone: nothing), and where the mouse is; the repeat runs only while the button is down
    static constexpr int railNone = -1000000;
    int railStrip = railNone;
    int railMouseY = 0;
    double railPressMs = 0.0; // when the rail press began (the steps are counted from it)
    int railSteps = 0;
    juce::TimedCallback railRepeat { [this] { stepHeldRail(); } };
    void pressRail (int strip, int mouseY);
    float railTarget (int strip) const;
    void stepRailOnce();
    juce::Rectangle<int> tipArea (Part part, int track) const;
    void setHover (Part part, int track);
    Part hoverPart = Part::none;
    int hoverTrack = -1;
    int headerDragTrack = -1;   // dragging a header sideways reorders the inserts
    // Ctrl(+Shift)+click / drag across the strips (FL): the range from the anchor to the mouse is selected (or removed)
    int selectAnchor = -1;
    bool selectAdding = true;
    std::vector<int> selectBase; // the selection kept under the range (Ctrl+Shift)
    void dragSelectTo (int track);
    bool headerDragged = false;
    void showSendMenu (int track);
    void addPreFaderItem (juce::PopupMenu& menu, int from, int track); // LYNX's pre-fader switch of a send (round 190aq)
    void routeSelection (int track, mixerops::RouteKind kind); // the selected inserts into `track` (FL's route arrow)
    void soloExclusive (int track);
    std::vector<int> selection { 0 };
    bool followPending = false; // a followPlaylistGroups edit is on its way (round 186c)
    std::vector<std::pair<int, float>> groupStart; // (track, value) of the selection when a group drag starts
    float groupGrab = 0.0f;
    void showMenu (int track);
    void askInsertCount(); // the "+" menu's "Increase number of tracks to..." (round 190fl)
    void askInsertMultiple (int at); // "Insert multiple..." / the "+" menu's "Add multiple..." (at most the room left)
    void showTrackEq (int track);
    ui::ValueSpec specFor (Part part, int track) const;
    void setValue (Part part, int track, double value);
    double valueOf (Part part, int track) const;

    AudioEngine& engine;
    int selected = 0;
    int visibleHeight = 400;

    Part dragPart = Part::none;
    int dragTrack = -1;
    float dragStartValue = 0.0f;
    ui::ValueDrag drag;

    std::vector<meters::PeakHold> levels; // per track*2: bar, peak hold and clip latch
    std::vector<float> inputLevels;       // per track: input level (decaying)
    std::vector<int> grShown;             // per track: the gain-reduction meter's drawn height (-1: none)
    ui::FrameTicker frames { *this, [this] (double ms) { return liveFrame (ms); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrips)
};

// Effects panel for the selected track, in FL's layout (REA round 104): the audio input row, 10 slots, the
// knobs of the chosen slot (LYNX), then the insert's built-in 3-band EQ and its audio output target.
//  - click an empty slot ("Slot N"): choose an effect; click an occupied one: edit its knobs; double click: its window
//  - the arrow on the left of a slot (or right-click): the slot's options (add / replace, move, delete...)
//  - on the right of a slot: the mix knob (drag; double-click resets to 100%) and the LED (on/off, bypass)
//  - EQ: drag a band's handle in the graph (frequency and gain; wheel: width), its fader (gain) or its knobs
//    (frequency, width)
class EffectsPanel final : public juce::Component,
                           public ui::HintSource,
                           public juce::DragAndDropTarget,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    // Effect presets (.fst) dragged from the Browser or Explorer, and effects dragged from the Browser's Plugin
    // database (round 162): dropped on a slot they replace it, anywhere else they go into the first free slot.
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool dropPreset (const juce::File& file, juce::Point<int> position);
    bool dropEffect (EffectSlot fx, juce::Point<int> position);
    // A generator dragged from the Plugin database onto the panel (a slot or anywhere else): as on the strip, a new
    // channel routed to the panel's insert; the slots don't change (FL 2026, round 167u).
    std::function<void (int, ChannelDef)> onInstrumentDropped;

    explicit EffectsPanel (AudioEngine& engine);
    ~EffectsPanel() override;

    void setTrack (int track);
    int getSelectedSlot() const noexcept { return selectedSlot; }
    juce::Rectangle<int> slotBoundsForCheck (int row) const { return slotBounds (row); } // --selftest (ui/MixerBasicsCheck.h)
    void deleteSlot (int slot); // removes the slot's effect (its menu's Delete, the Delete key)
    void groupIntoRack (int slot, bool all); // round 183c: Live's Group, the slot (or the chain) into an Audio Effect Rack
    void selectSlot (int slot)
    {
        selectedSlot = slot;
        repaint();
    }
    void openSlotEditor (int slot) { openEditor (slot); }

    // Round 183a: the Detail View's device cards use the slots' own menus and windows through these, so the Mixer and
    // the Device View stay one code path. The menus open over `target` (nullptr: this panel, the Mixer's own case).
    void setMenuTarget (juce::Component* target) noexcept { menuTarget = target; }
    void showSlotMenuFor (int slot)
    {
        selectedSlot = slot;
        showSlotMenu (slot);
    }
    void showAddMenuFor (int row) { showAddMenu (row); }
    std::function<void (juce::PopupMenu&, int slot)> extendSlotMenu; // items put first in a slot's menu (nullptr: none)
    static inline juce::PopupMenu* slotMenuForSnapshot = nullptr;    // the shot routes: the next slot menu goes here instead of opening
    // Round 190ce: the slot menu's "Hot-swap in the Browser" (set once by the main window; nullptr: no such item)
    static inline std::function<void (int track, int slot)> onHotSwapSlot;
    // the knob of parameter `param` of effect `slot` (param -1: the slot's Mix), as the Mixer shows and sets it
    ui::ValueSpec slotParamSpec (int param, int slot) const { return specFor (param < 0 ? mixParam : param, slot); }
    double slotParamValue (int param, int slot) const { return valueOf (param < 0 ? mixParam : param, slot); }
    void setSlotParam (int param, int slot, double value) { setValue (param < 0 ? mixParam : param, slot, value); }
    void toggleSlotEditor (int slot) { toggleEditor (slot); }

    // Sizes the panel for a view of `viewWidth` x `viewHeight` (round 163): when the slots and the EQ don't both fit,
    // the EQ folds to its title bar (click it to unfold); when even that doesn't fit, the panel is taller than the
    // view and the viewport around it scrolls. Nothing overlaps at any height or UI zoom.
    void fitToView (int viewWidth, int viewHeight, int scrollBarThickness);
    bool isEqFolded() const noexcept { return eqFolded; }
    // the heights the panel needs with the EQ open and folded (the knobs of the selected effect may be cut below)
    int neededHeight (bool folded) const;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showTip (int param, int slot, bool transient);
    // the input row above the slots (inserts only): arm switch, input source box with its level
    juce::Rectangle<int> armBounds() const;
    juce::Rectangle<int> inputBoxBounds() const;
    int hoverInput = 0; // 1 = arm, 2 = input box
    std::array<float, 2> inputLevel {};
    int hoverSlot = -1, hoverParam = -1;  // hover feedback: the slot row, the knob (mixParam = a slot's dry/wet)
    bool hoverLed = false;
    ui::ValueSpec specFor (int param, int slot) const; // param = mixParam: the slot's dry/wet
    double valueOf (int param, int slot) const;
    void setValue (int param, int slot, double value);
    int mixKnobAt (juce::Point<int> p) const; // slot whose dry/wet knob is at p, or -1
    juce::Rectangle<int> slotBounds (int slot) const;
    juce::Rectangle<int> mixBounds (int slot) const; // dry/wet knob on the slot's row
    static constexpr int mixParam = -2;
    // FL's layout (REA round 104)
    juce::Rectangle<int> arrowBounds (int slot) const; // the slot's options arrow
    juce::Rectangle<int> ledBounds (int slot) const;   // the slot's on/off LED
    int bottomTop() const;                             // top of the EQ + output section
    bool eqShown() const;                              // false in a clean mixer (round 163)
    int belowHeight (bool folded) const;               // height of the EQ + output section
    juce::Rectangle<int> eqFoldBounds() const;         // the EQ's title bar (click: fold / unfold)
    void refit();
    bool eqFolded = false;
    int viewWidth = 0, viewHeight = 0, scrollBarThickness = 0;
    static inline int eqFoldChoice = 0;                // 0 automatic, 1 open (the user unfolded it), 2 folded by the user
    juce::Rectangle<int> eqGraphBounds() const;
    juce::Rectangle<int> eqFaderBounds (int band) const;
    juce::Rectangle<int> eqKnobBounds (int band, int what) const; // what: 1 = frequency, 2 = width
    juce::Rectangle<int> outputBounds() const;
    juce::Point<float> eqHandle (int band) const;      // where a band's handle sits in the graph
    int eqControlAt (juce::Point<int> p) const;        // an EQ control under p (eqParamBase/eqPointBase codes), or 0
    void paintEq (juce::Graphics& g);
    void setEqFromGraph (int band, juce::Point<int> p);
    void showOutputMenu();
    juce::String outputName() const;
    static constexpr int eqParamBase = -100; // eqParamBase - band * 3 - what (0 gain, 1 frequency, 2 width), stored 0..1
    static constexpr int eqPointBase = -200; // eqPointBase - band: dragging the band's handle in the graph
    static bool isEqParam (int p) noexcept { return p <= eqParamBase && p > eqParamBase - 9; }
    static bool isEqPoint (int p) noexcept { return p <= eqPointBase && p > eqPointBase - 3; }
    int hoverEq = 0;
    bool hoverOutput = false, hoverArrow = false;
    static constexpr int gateStepParam = -3; // drawing the volume gate steps
    juce::Rectangle<int> knobBounds (int param) const;
    juce::Rectangle<int> knobBoundsIn (int param, int areaBottom) const; // laid out above areaBottom
    int selectedEffectBottom() const; // the lowest edge of the selected effect's controls (laid out from the top)
    int fittedSlot = -2, fittedKind = -2; // the selected effect the panel's height was fitted to
    bool gateSelected() const;
    juce::Rectangle<int> gateStepsBounds() const;
    void setGateStep (juce::Point<int> p);
    void showGatePatterns();
    static constexpr int bandLevelParam = -4; // drawing the Fruity Vocoder's band levels
    bool vocoderSelected() const;
    juce::Rectangle<int> bandLevelsBounds() const;
    void setBandLevel (juce::Point<int> p);
    void showBandLevelMenu();
    bool tuneSelected() const;                // LYNX Tune: its 12 note switches are a keyboard strip
    juce::Rectangle<int> tuneKeysBounds() const;
    void toggleTuneNote (juce::Point<int> p);
    int knobAt (juce::Point<int> p) const;
    const std::vector<EffectSlot>* slots() const;
    void showAddMenu (int slot);
    void showSlotMenu (int slot);
    juce::Rectangle<int> pluginButtonBounds() const;
    void openEditor (int slot);
    // Freeze (round 190ff): a track freeze holds this insert's effects (not loaded): a click, wheel or drop on the slots
    // (the FX switch, the side-chain button) is refused with a notice; the EQ, fader and routing stay live. True = refused.
    bool refuseFrozenSlots (juce::Point<int> p);
    void toggleEditor (int slot); // FL: a click on the slot's name shows or hides its window (round 170b)
    juce::Rectangle<int> editorButtonBounds() const;
    // Round 177b: the selected effect's "Sidechain from" picker (effects with a side input; empty bounds = none)
    juce::Rectangle<int> sidechainButtonBounds() const;
    int titleRowY() const;
    void paintSidechainButton (juce::Graphics& g, const EffectSlot& slot);
    bool hoverSidechain = false;
    void openPluginWindow (int slot);
    juce::Rectangle<int> fxSwitchBounds() const; // all effects on/off; right-click opens/closes their windows
    void toggleAllWindows();
    int slotAt (juce::Point<int> p) const;
    int dragSlot = -1, dropSlot = -1; // dragging a slot (an effect index) to another slot (a row)
    // the rows of FL's slot places (round 170b): the row an effect shows in, the effect in a row (-1 = empty)
    int rowOf (int index) const;
    int effectIn (int row) const;
    double wheelAccumulator = 0.0; // a trackpad's small wheel steps over a slot, added up
    int wheelSlot = -1;
    juce::uint32 wheelTime = 0;
    // A/B compare: a spare state per (insert, slot), swapped with the live one on request (not saved); shared by every
    // panel (round 183a: the Device View's cards reach the same B as the Mixer)
    static inline std::map<std::pair<int, int>, EffectSlot> spare;
    juce::Component* menuTarget = nullptr; // (round 183a) where the slot menus open; nullptr = this panel
    juce::Component* menuOwner() noexcept { return menuTarget != nullptr ? menuTarget : this; }
    std::unique_ptr<juce::FileChooser> sampleChooser; // Newtone / Newtime source sample

    AudioEngine& engine;
    int track = 0;
    int selectedSlot = -1;
    int dragParam = -1;
    float dragStart = 0.0f;
    ui::ValueDrag drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
    std::unique_ptr<ui::TimerWhileShowing> showingTimer; // the polling timer, only while on screen (round 174f)
};

// FL's "Mixer layout" drop-down in the Mixer's title bar (issue #52, REA round 168h): the current layout's name and a
// chevron; a click lists Compact, Compact 2, Compact alt | Wide, Wide 2, Wide 3, Wide alt | Extra large.
class MixerLayoutBox final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit MixerLayoutBox (MixerStrips& strips);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    static juce::PopupMenu menu (MixerStrips& strips); // also in the mixer menu's View section
    static constexpr int width = 112;

private:
    MixerStrips& strips;
};

class MixerComponent final : public juce::Component
{
public:
    explicit MixerComponent (AudioEngine& engine);
    void resized() override;

    // Selects the first track that has effects (used by --snapshot mode).
    void selectFirstTrackWithEffects();
    void selectTrack (int track); // also scrolls the strip into view
    void selectSlot (int slot) { effects.selectSlot (slot); }
    int getSelectedTrack() const noexcept { return strips.getSelectedTrack(); }
    // Round 183a: the strips' meters run (they are on screen), and the level one shows.
    bool metersShowing() const { return strips.isShowing(); }
    float shownLevel (int track, int side) const noexcept { return strips.shownLevel (track, side); }
    // Selects the insert and slot and opens the effect's editor (the Browser's "Current project > Effects")
    void openSlotEditor (int track, int slot)
    {
        selectTrack (track);
        effects.selectSlot (slot);
        effects.openSlotEditor (slot);
    }

    // The key map's mixer.* actions.
    bool runAction (const std::string& id);

    // Adds an effect to the first free slot of the selected insert (quick-add palette). False when full.
    bool addEffectToSelected (EffectSlot slot);
    // A mixer preset opened from the Browser (FL: onto the selected tracks)
    bool loadPresetOnSelection (const juce::File& file);
    // A generator dragged from the Browser onto a strip (the owner has the Channel Rack that adds the channel).
    std::function<void (int, ChannelDef)> onInstrumentDropped;
    MixerStrips& getStripsForSnapshot() { return strips; }
    MixerLayoutBox& getLayoutBox() { return layoutBox; } // the main window puts it in the Mixer's title bar

    static constexpr int effectsWidth = 300;

private:
    AudioEngine& engine;
    MixerStrips strips;
    juce::Viewport viewport;
    juce::Viewport effectsView; // the effects panel scrolls when the mixer is short (round 163)
    EffectsPanel effects;
    MixerLayoutBox layoutBox { strips };
};
} // namespace lynx
