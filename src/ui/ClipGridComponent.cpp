#include "ui/TextCache.h"
#include "ui/ClipGridComponent.h"
#include "ui/TrackerComponent.h"
#include "ui/PaintAB.h"
#include "ui/PlaylistComponent.h"
#include "core/SectionOps.h" // round 190ol (P1: scenes from sections)
#include "ui/PluginOffTag.h"
#include "core/CrossPaste.h"   // issue #232 stage 2 (round 190nz)
#include "ui/WindowClipboard.h"

#include <algorithm>
#include <set>
#include <cmath>
#include <cstring>

#include "core/ClipLauncher.h"
#include "core/ClipLoopOps.h"
#include "core/Comping.h"
#include "core/TrackStates.h"
#include "core/SessionMixer.h"
#include "core/DetailChain.h"
#include "core/PlaylistOps.h"
#include "ui/PluginDropUi.h"
#include "core/Groove.h"
#include "core/GrooveAudio.h"
#include "core/SessionKeys.h"
#include "core/SessionOps.h"
#include "engine/SampleLibrary.h"
#include "ui/BrowserComponent.h"
#include "ui/ColourPicker.h"
#include "ui/DetailView.h"
#include "ui/HintBar.h"
#include "ui/MapMode.h"
#include "ui/Motion.h"
#include "ui/GroovePoolComponent.h"
#include "ui/SessionMixer.h"
#include "ui/ExportDialog.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/WorkflowExtras.h" // round 190iu (issue #191.2)
#include "ui/Theme.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
namespace
{
constexpr int footerHeight = 30;
// round 190ig: the Live look of the clip grid (View › "Classic LYNX look for the clip grid" turns it off: round 181a's look)
bool classicLook = false;
bool liveLook() noexcept { return ! classicLook && layoutstyle::current() == layoutstyle::Style::clipLaunch; }
// A size of the grid in round 181a's look and in Live's (round 190ig, CL-3: measured in live-session.png by rounds 190ia
// and 190ig): reads as an int, the look's value.
struct LookMetric
{
    int classic, live;
    operator int() const noexcept { return liveLook() ? live : classic; }
};
// Live: a 17 px header over a 2 px separator, 17 px slots over a 1 px separator, 74 px columns with 2 px between them,
// a 16 px launch zone then a 1 px divider (live-session.png x 396..412: triangle 400..407, divider 412), a 17 px status
// row under a 2 px separator (live-clipview-audio.png y 395..413)
const LookMetric headerHeight { 26, 19 }, rowHeight { 24, 18 }, columnWidth { 104, 76 }, launchZone { 20, 17 };
// the scene column: Live's 84 px cells after a 2 px separator (round 190ia CL-14b, live-session.png x 1540..1625)
int sceneBaseWidthNow() noexcept { return liveLook() ? 86 : 128; }
constexpr int minColumns = 8, minRows = 8;
// round 190bv: the row under the slots with each track's stop and arm buttons
const LookMetric statusHeight { 22, 19 };
constexpr int armWidth = 22;
constexpr const char* gridDragDescription = "lynx-grid-clip";
constexpr int fixedLengths[] = { 1, 2, 4, 8, 16, 32 };
// round 190ef: the reference's View › "Scene Tempo And Time Signature" (a preference): each scene shows a tempo field
// and a time signature field at its right; the scene column grows by their width
constexpr int tempoFieldWidth = 46, signatureFieldWidth = 42, sceneFieldsWidth = tempoFieldWidth + signatureFieldWidth + 4;
bool sceneFieldsOn = false;
// Live's two scene-launch preferences in the Main track menu (round 190ld, Live 12.4 shots/ref/live-scene-menu-queued.png):
// "Select Next Scene on Launch" (on, as LYNX always did) and "Trigger Recording on Launch" (details DL-36; off)
bool selectNextOnLaunch = true, recordOnSceneLaunch = false;
// round 190em: Live 12.4.16's launch triangle of a scene without its own tempo / signature (190ei shot: #1E1E1E)
const juce::Colour liveSceneTriangle { 0xff1e1e1e };
// round 190ig (CL-11, CL-12): a waiting launch in Live pulses: its triangle (a waiting stop: the track's stop square)
// fades from black to the playing green #00D38D and back, one pulse every 400 ms whatever the tempo — filmed in Live
// 12.4 (round 190ig: 10 pulses at 167 BPM and again at 100 BPM, best-fit period 400.0 ms both; a waiting stop's square
// the same, /mnt/e/lynx-work/design190ig/s4/film1..3). Round 181a's look keeps its 150 ms on / off blink.
constexpr int liveBlinkPeriodMs = 400;
constexpr int classicBlinkPhaseMs = 150;
// --design190igs4shot: the blink phase drawn (0 dark, 1 lit — round 190ia's two stills #002217 / #008A5C; -1: the clock's)
int blinkPhaseForSnapshot = -1;
// the blink's phase counter the repaint hashes compare: the Live pulse moves at the grid's 30 Hz; with Settings ›
// General › Animations off it stays still (no phase repaints)
std::uint32_t blinkPhaseNow() noexcept
{
    if (! liveLook())
        return juce::Time::getMillisecondCounter() / (juce::uint32) classicBlinkPhaseMs;
    if (! ui::motion::enabled())
        return 0;
    return juce::Time::getMillisecondCounter() / 33u;
}
// the pulse's level now, 0 (black) .. 1 (full colour); Animations off: Live's lit still #008A5C (0.65)
float livePulse() noexcept
{
    if (blinkPhaseForSnapshot >= 0)
        return blinkPhaseForSnapshot == 1 ? 0.655f : 0.16f;
    if (! ui::motion::enabled())
        return 0.655f;
    const auto phase = (double) (juce::Time::getMillisecondCounter() % (juce::uint32) liveBlinkPeriodMs) / (double) liveBlinkPeriodMs;
    return (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase));
}
int sceneWidthNow() noexcept { return sceneBaseWidthNow() + (sceneFieldsOn ? sceneFieldsWidth : 0); }
constexpr const char* playlistDragDescription = "lynx-playlist-clip";

const std::pair<LaunchQuant, const char*> quantNames[] = {
    { LaunchQuant::global, TR_NOOP ("Global") }, { LaunchQuant::none, TR_NOOP ("None") }, { LaunchQuant::bars8, TR_NOOP ("8 bars") },
    { LaunchQuant::bars4, TR_NOOP ("4 bars") },  { LaunchQuant::bars2, TR_NOOP ("2 bars") }, { LaunchQuant::bar, TR_NOOP ("1 bar") },
    { LaunchQuant::half, "1/2" }, { LaunchQuant::halfTriplet, "1/2T" }, { LaunchQuant::quarter, "1/4" }, { LaunchQuant::quarterTriplet, "1/4T" },
    { LaunchQuant::eighth, "1/8" }, { LaunchQuant::eighthTriplet, "1/8T" }, { LaunchQuant::sixteenth, "1/16" },
    { LaunchQuant::sixteenthTriplet, "1/16T" }, { LaunchQuant::thirtySecond, "1/32" }
};

const std::pair<LaunchMode, const char*> modeNames[] = { { LaunchMode::trigger, TR_NOOP ("Trigger") }, { LaunchMode::gate, TR_NOOP ("Gate") },
                                                         { LaunchMode::toggle, TR_NOOP ("Toggle") }, { LaunchMode::repeat, TR_NOOP ("Repeat") } };
const std::pair<FollowKind, const char*> followNames[] = {
    { FollowKind::none, TR_NOOP ("No action") }, { FollowKind::stop, TR_NOOP ("Stop") }, { FollowKind::playAgain, TR_NOOP ("Play again") },
    { FollowKind::previous, TR_NOOP ("Previous") }, { FollowKind::next, TR_NOOP ("Next") }, { FollowKind::first, TR_NOOP ("First") },
    { FollowKind::last, TR_NOOP ("Last") }, { FollowKind::any, TR_NOOP ("Any") }, { FollowKind::other, TR_NOOP ("Other") }
};

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// The Session Record button (round 190bv): a ring on a square button, red while slots record or overdub.
class RingButton final : public juce::Button
{
public:
    RingButton() : juce::Button ({}) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        namespace c = theme::colour;
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        theme::fillRoundRect (g, box, 3.0f, down ? c::bg6 : over ? c::bg5 : c::bg4);
        const auto ring = box.withSizeKeepingCentre (11.0f, 11.0f);
        g.setColour (getToggleState() ? c::record : c::textPrimary);
        g.drawEllipse (ring, 2.2f);
    }
};

// (what the grid copied last, with its slot settings; the Playlist's clipboard holds the same clip for a paste there)
std::optional<session::SlotClip> copiedSlot;
std::uint64_t copiedSlotStamp = 0;
// (round 190lx, DL-29: a rectangle of slots copied or cut; the Playlist's clipboard gets its first clip)
std::optional<session::SlotBlock> copiedBlock;

class ClipGridComponent final : public juce::Component,
                                public ui::HintSource,
                                public mapmode::SpotSource,
                                public juce::DragAndDropTarget,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer,
                                private juce::ComponentMovementWatcher
{
public:
    explicit ClipGridComponent (AudioEngine& e) : juce::ComponentMovementWatcher (this), engine (e)
    {
        setWantsKeyboardFocus (true);
        setTitle (tr ("Clip grid"));
        setAccessible (true);
        for (auto* b : { &stopAll, &backToPlaylist, &grooves, &mixerButton })
        {
            addAndMakeVisible (*b);
            b->setConnectedEdges (0);
        }
        addAndMakeVisible (sessionRecord);
        sessionRecord.setTitle (tr ("Session Record"));
        sessionRecord.setTooltip (tr ("Session Record: armed tracks record into the selected scene; while their clips play, notes are added to them (overdub)"));
        sessionRecord.onClick = [this] {
            engine.toggleSessionRecord (selectedScene);
            syncControls();
        };
        grooves.setButtonText (tr ("Grooves"));
        grooves.setTooltip (tr ("Show or hide the Groove Pool"));
        grooves.setClickingTogglesState (true);
        grooves.onClick = [this] { showGroovePool (grooves.getToggleState()); };
        // round 190dr: the Session mixer under the slots, and Live's View > Mixer / Mixer Controls items on a button
        mixer = std::make_unique<SessionMixerComponent> (engine);
        addAndMakeVisible (*mixer);
        mixerButton.setButtonText (tr ("Mixer"));
        mixerButton.setTooltip (tr ("Show or hide the mixer under the clips, and its sections"));
        mixerButton.onClick = [this] {
            juce::PopupMenu menu;
            mixer->addViewItems (menu);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mixerButton));
        };
        stopAll.setButtonText (tr ("Stop all clips"));
        mapmode::tag (stopAll, remotemap::Target::of (RemoteAction::stopAllClips)); // (round 190ef)
        mapmode::tag (sessionRecord, remotemap::Target::of (RemoteAction::sessionRecord));
        stopAll.onClick = [this] { engine.launcherCommand ({ launcher::Command::Type::stopAll }); };
        backToPlaylist.setButtonText (tr ("Back to Playlist"));
        backToPlaylist.setTooltip (tr ("Tracks that launched a clip stay silent in the Playlist until this is pressed"));
        backToPlaylist.onClick = [this] { engine.launcherCommand ({ launcher::Command::Type::backToArrangement }); };
        addAndMakeVisible (globalQuant);
        globalQuant.setTooltip (tr ("Launch quantisation: when a launched clip starts"));
        int id = 1;
        for (const auto& [q, name] : quantNames)
        {
            if (q != LaunchQuant::global)
                globalQuant.addItem (q == LaunchQuant::none || q <= LaunchQuant::bar ? tr (name) : juce::String (name), id);
            ++id;
        }
        globalQuant.onChange = [this] {
            const auto q = (LaunchQuant) (globalQuant.getSelectedId() - 1);
            engine.editSong ([q] (Song& s) { launcher::ensureGrid (s).globalQuant = q; }, false);
        };
        syncControls();
    }

    void resized() override
    {
        for (auto* c : std::initializer_list<juce::Component*> { &stopAll, &backToPlaylist, &sessionRecord, &grooves, &mixerButton, &globalQuant })
            c->setVisible (buttonRow); // (round 190iv: Live's places instead, unless the row is asked for)
        auto foot = getLocalBounds().removeFromBottom (footerNow()).withTrimmedRight (0).reduced (4, 3);
        auto right = foot.removeFromRight (sceneWidthNow() - 8);
        globalQuant.setBounds (right);
        foot.removeFromRight (6);
        const auto small = getWidth() < 520;
        stopAll.setBounds (foot.removeFromLeft (small ? 84 : 110));
        foot.removeFromLeft (6);
        backToPlaylist.setBounds (foot.removeFromLeft (small ? 104 : 124));
        foot.removeFromLeft (6);
        sessionRecord.setBounds (foot.removeFromLeft (foot.getHeight()));
        foot.removeFromLeft (6);
        grooves.setBounds (foot.removeFromLeft (juce::jmin (72, juce::jmax (0, foot.getWidth()))));
        foot.removeFromLeft (6);
        mixerButton.setBounds (foot.removeFromLeft (juce::jmin (64, juce::jmax (0, foot.getWidth()))));
        if (groovePool != nullptr && groovePool->isVisible())
            groovePool->setBounds (0, getHeight() - footerNow() - poolHeight(), getWidth(), poolHeight());
        const auto cells = cellArea();
        mixer->setBounds (0, cells.getBottom() + statusHeight + mapStripNow(), getWidth(), mixerHeight());
        clampScroll();
        syncControls();
    }

    void paint (juce::Graphics& g) override
    {
        namespace c = theme::colour;
        const auto live = liveLook();
        g.fillAll (live ? theme::livelook::separator : c::bg1);
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        const auto* state = engine.launcherState();
        const auto& song = engine.getSong();
        const auto cells = cellArea();
        const auto columns = columnCount();
        const auto scenes = rowCount();
        if (live)
        {
            // Live (round 190ig, CL-8/CL-9; live190cw session190bv-02a): right of the last track the Session is #363636,
            // the selected scene's row crossing it in #464646
            const auto lastX = columnX (columns);
            if (lastX < cells.getRight())
            {
                const juce::Rectangle<int> rest (lastX, headerHeight, cells.getRight() - lastX, cells.getHeight() + statusHeight);
                g.setColour (theme::livelook::surface);
                g.fillRect (rest);
                if (selectedScene >= 0 && selectedScene < scenes)
                {
                    g.setColour (theme::livelook::raised);
                    g.fillRect (rest.getIntersection ({ lastX, rowY (selectedScene), rest.getWidth(), (int) rowHeight - 1 }).getIntersection (cells));
                }
            }
        }

        // column headers, slots and the stop row, clipped to the scrolling part
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, cells.getRight(), getHeight()));
            // round 190gr (A11): a repaint of a few cells (a playing clip's progress, a blink) draws only what it reaches;
            // each part draws inside its own box, so the pixels are the same
            const auto cull = ui::paintCache();
            const auto reaches = [&g, cull] (juce::Rectangle<int> box) { return ! cull || g.clipRegionIntersects (box); };
            // round 190op (P4): deactivated tracks and hidden ones shown (nothing looked up per column while there are none)
            const auto anyStates = deactivate::anyMarked (song) || (PlaylistComponent::showHiddenTracks && trackstates::hiddenCount (song) > 0);
            for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
            {
                const auto x = columnX (t);
                if (! reaches ({ x, 0, columnWidth, cells.getBottom() + statusHeight }))
                    continue;
                if (reaches ({ x, 0, columnWidth, headerHeight }))
                {
                    paintHeader (g, song, state, t, { x, 0, columnWidth, headerHeight });
                    if (anyStates)
                        paintTrackStateMark (g, song, t, { x, 0, columnWidth, headerHeight });
                }
                if (reaches ({ x, cells.getBottom(), columnWidth, statusHeight }))
                    paintStatus (g, song, state, t, { x, cells.getBottom(), columnWidth, statusHeight });
                juce::Graphics::ScopedSaveState rows (g);
                g.reduceClipRegion (juce::Rectangle<int> (x, headerHeight, columnWidth, cells.getHeight()));
                for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
                    if (reaches ({ x, rowY (r), columnWidth, rowHeight }))
                        paintSlot (g, song, *grid, state, t, r, { x, rowY (r), columnWidth, rowHeight });
                if (live && rowY (scenes) < cells.getBottom()) // (Live: the column goes on under the last scene, CL-8)
                {
                    g.setColour (t == selectedTrack ? theme::livelook::raised : theme::livelook::surface);
                    g.fillRect (juce::Rectangle<int> (x, rowY (scenes), (int) columnWidth - 2, cells.getBottom() - rowY (scenes)).getIntersection (cells));
                }
            }
        }

        // the scene column stays in place
        paintReturnColumns (g, cells); // (round 190kc: Live's return tracks, between the tracks and the Main column)
        const juce::Rectangle<int> sceneColumn (sceneX(), 0, sceneWidthNow(), cells.getBottom() + statusHeight);
        if (liveLook())
        {
            // Live's Main column (round 190ia CL-14b, live-session.png x 1540..1625 y 86..240): a 2 px separator, the "Main"
            // header (#3C3C3C, its name in Live's text grey) over a 2 px separator, then the scene cells on #363636
            namespace lv = theme::livelook;
            g.setColour (lv::separator);
            g.fillRect (sceneColumn);
            g.setColour (lv::surface);
            g.fillRect (sceneColumn.withTrimmedLeft (2).withTrimmedTop (headerHeight));
            const auto head = sceneColumn.withTrimmedLeft (2).withHeight (headerHeight - 2);
            g.setColour (lv::mainHeader);
            g.fillRect (head);
            g.setColour (lv::text);
            g.setFont (liveFont());
            textcache::drawText (g, tr ("Main"), head.withTrimmedLeft (8), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (c::bg2);
            g.fillRect (sceneColumn);
            g.setColour (c::border);
            g.fillRect (sceneColumn.getX(), 0, 1, sceneColumn.getHeight());
            g.setColour (c::textSecondary);
            g.setFont (theme::font (theme::type::small, true));
            textcache::drawText (g, tr ("Scenes"), sceneColumn.withHeight (headerHeight).withTrimmedLeft (8), juce::Justification::centredLeft);
        }
        if (! ui::paintCache() || g.clipRegionIntersects (sceneColumn.withTrimmedTop (headerHeight)))
        {
            juce::Graphics::ScopedSaveState rows (g);
            g.reduceClipRegion (sceneColumn.withTrimmedTop (headerHeight));
            for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
                paintScene (g, *grid, r, { sceneColumn.getX(), rowY (r), sceneWidthNow(), rowHeight });
        }
        paintMapStrip (g); // (round 190kc)
        g.setColour (live ? theme::livelook::separator : c::border); // (the status row's top edge, across the tracks and the scene column)
        g.fillRect (0, cells.getBottom(), getWidth(), 1);
        // a clip or a file dragged over a slot: where it would land
        if (dropTrack >= 0 && dropScene >= 0)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (cells);
            g.setColour (c::accent);
            g.drawRect (juce::Rectangle<int> (columnX (dropTrack), rowY (dropScene), columnWidth, rowHeight).reduced (1), 2);
        }

        // a device dragged over a track: that whole column (round 190dr)
        if (deviceDropTrack >= 0)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, cells.getRight(), cells.getBottom() + statusHeight));
            g.setColour (c::accent);
            g.drawRect (juce::Rectangle<int> (columnX (deviceDropTrack), 0, columnWidth, cells.getBottom() + statusHeight).reduced (1), 2);
        }

        // round 190iv: without the button row, Stop All Clips is the Main column's status cell (Live: a 9 x 9 square there,
        // design190ia CL-25, ref/live-session.png x 1550 y 566)
        if (! buttonRow)
        {
            const auto stop = stopAllBox();
            g.setColour (c::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (tr ("Stop all clips"), stop.withX (stop.getRight() + 6).withWidth (sceneWidthNow() - 30), juce::Justification::centredLeft, true);
            // (round 190oj CS-10: a solid square as Live's and as the tracks' stop squares; with a light outline it read
            // as an unticked checkbox)
            g.setColour (stopAllDown ? c::bg6 : juce::Colour (0xff1e1e1e));
            g.fillRect (stop.withSizeKeepingCentre (9, 9));
            return;
        }
        // the footer: controls on the scene side and the buttons, on a band
        const auto foot = getLocalBounds().removeFromBottom (footerHeight);
        g.setColour (c::bg3);
        g.fillRect (foot);
        g.setColour (c::border);
        g.fillRect (foot.getX(), foot.getY(), foot.getWidth(), 1);
    }

    // Round 190lr (DL-20, capture live-hover-*.png): in Live's look the button under the pointer grows by 2 px in its own
    // colour (a clip's launch ▶ 7 -> 9 px, an empty slot's ■ / an armed slot's ●, a track's Stop ■ and a scene's ▶ 9 -> 11
    // px); a clip's body, a slot's body and a scene's name don't change. Only the two cells concerned repaint.
    enum class HoverButton { none, slot, trackStop, scene };
    HoverButton hoverButton = HoverButton::none;
    int hoverTrack = -1, hoverScene = -1;
    juce::Rectangle<int> hoverCell (HoverButton b, int track, int scene) const
    {
        switch (b)
        {
            case HoverButton::slot: return cellForSnapshot (0, track, scene);
            case HoverButton::trackStop: return cellForSnapshot (1, track, 0);
            case HoverButton::scene: return cellForSnapshot (2, 0, scene);
            default: return {};
        }
    }
    void setHoverButton (HoverButton b, int track, int scene)
    {
        if (b == hoverButton && track == hoverTrack && scene == hoverScene)
            return;
        repaint (hoverCell (hoverButton, hoverTrack, hoverScene));
        hoverButton = b, hoverTrack = track, hoverScene = scene;
        repaint (hoverCell (hoverButton, hoverTrack, hoverScene));
    }
    bool hovering (HoverButton b, int track, int scene) const noexcept { return hoverButton == b && hoverTrack == track && hoverScene == scene; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! liveLook())
            return setHoverButton (HoverButton::none, -1, -1);
        const auto p = e.getPosition();
        if (const auto t = statusTrackAt (p); t >= 0)
            return setHoverButton (p.x - columnX (t) < launchZone ? HoverButton::trackStop : HoverButton::none, t, -1);
        const auto hit = hitTestCell (p);
        if (hit.scene >= 0 && hit.track < 0)
            return setHoverButton (p.x < sceneX() + launchZone && sceneFieldAt (p, hit.scene) == 0 ? HoverButton::scene : HoverButton::none, -1, hit.scene);
        if (hit.track >= 0 && hit.scene >= 0 && p.x - columnX (hit.track) < launchZone)
            return setHoverButton (HoverButton::slot, hit.track, hit.scene);
        setHoverButton (HoverButton::none, -1, -1);
    }
    void mouseExit (const juce::MouseEvent&) override { setHoverButton (HoverButton::none, -1, -1); }
    void hoverForSnapshot (int kind, int track, int scene) // (--details190lrshot: 0 none, 1 a slot's button, 2 a track's Stop, 3 a scene's ▶)
    {
        setHoverButton ((HoverButton) kind, track, scene);
    }

    // Round 190lr (DL-29, Live manual 7.4 "click into an empty slot and 'rubber-band' select from there"; capture
    // live-multisel-L21..23): Shift+click selects the rectangle from the selected slot to the clicked one, a drag from an
    // empty slot (outside its button) selects the slots it covers; Delete clears them all. Ctrl+click replaces the
    // selection as in Live 12.4.16 (filmed). The rectangle: tracks rangeTrack0..selectedTrack, scenes rangeScene0..selectedScene
    // (−1: one slot).
    int rangeTrack0 = -1, rangeScene0 = -1;
    bool rubberBand = false;
    bool rangeLive() const noexcept { return rangeTrack0 >= 0 && ! sceneSelected && rangeEndTrack == selectedTrack && rangeEndScene == selectedScene; }
    int rangeEndTrack = -1, rangeEndScene = -1; // (the selection the rectangle was made with: any other selection ends it)
    void markRangeEnd() noexcept { rangeEndTrack = selectedTrack, rangeEndScene = selectedScene; }
    bool inRange (int t, int row) const noexcept
    {
        if (! rangeLive())
            return t == selectedTrack && row == selectedScene && ! sceneSelected;
        return t >= juce::jmin (rangeTrack0, selectedTrack) && t <= juce::jmax (rangeTrack0, selectedTrack) && row >= juce::jmin (rangeScene0, selectedScene)
               && row <= juce::jmax (rangeScene0, selectedScene);
    }
    void clearRange() noexcept { rangeTrack0 = rangeScene0 = -1; }
    void selectRangeForSnapshot (int t0, int s0, int t1, int s1)
    {
        sceneSelected = titleSelected = false;
        rangeTrack0 = t0, rangeScene0 = s0, selectedTrack = t1, selectedScene = s1;
        markRangeEnd();
        repaint();
    }
    int rangeCountForCheck() const
    {
        int n = 0;
        for (int t = 0; t < columnCount(); ++t)
            for (int r = 0; r < rowCount(); ++r)
                n += inRange (t, r) ? 1 : 0;
        return n;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragTrack = dragScene = -1;
        if (! buttonRow && stopAllBox().expanded (3).contains (e.getPosition()) && e.mods.isLeftButtonDown()) // (round 190iv)
        {
            stopAllDown = true;
            repaint (stopAllBox().expanded (4));
            engine.launcherCommand ({ launcher::Command::Type::stopAll });
            return;
        }
        const auto hit = hitTestCell (e.getPosition());
        // the status row (round 190bv): a track's stop button and its arm button (Ctrl: without disarming the others)
        if (const auto t = statusTrackAt (e.getPosition()); t >= 0)
        {
            if (e.mods.isPopupMenu())
                return;
            const auto x = e.x - columnX (t);
            if (x >= columnWidth - armWidth - 6 && ! mixer->armsShown()) // (round 190dr: with the mixer's Volume section the arm is there, as in Live)
                engine.setGridTrackArmed (t, ! engine.isGridTrackArmed (t), ! e.mods.isCommandDown());
            else if (x < launchZone)
                engine.launcherCommand ({ launcher::Command::Type::stopTrack, t });
            repaint();
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (const auto t = headerTrackAt (e.getPosition()); t >= 0) // (round 190ay: the track's own menu)
                showHeaderMenu (t);
            else if (hit.scene >= 0 && hit.track < 0)
            {
                if (const auto field = sceneFieldAt (e.getPosition(), hit.scene); field != 0)
                    showSceneFieldMenu (hit.scene, field);
                else
                    showSceneMenu (hit.scene);
            }
            else if (hit.track >= 0 && hit.scene >= 0)
                showSlotMenu (hit.track, hit.scene);
            return;
        }
        // round 190lr (DL-38, Live's single-track Back to Arrangement): a click on a taken track's mark gives that track's
        // Playlist clips back at once (the others stay with the grid)
        if (const auto t = headerTrackAt (e.getPosition()); t >= 0 && e.x >= columnX (t) + columnWidth - 22 && trackTaken (t))
        {
            engine.launcherCommand ({ launcher::Command::Type::trackBackToArrangement, t });
            repaint();
            return;
        }
        if (const auto t = headerTrackAt (e.getPosition()); t >= 0) // a column's title selects its track (round 183a)
        {
            // round 190lr (DL-109, Live manual 18.2 "Multiple adjacent or nonadjacent tracks can be selected at once by
            // Shift-clicking or Ctrl-clicking"): the Session mixer's edits then act on all of them
            if (e.mods.isShiftDown() && selectedTrack >= 0)
            {
                trackSet.clear();
                for (int k = juce::jmin (selectedTrack, t); k <= juce::jmax (selectedTrack, t); ++k)
                    trackSet.insert (k);
            }
            else if (e.mods.isCommandDown())
            {
                if (trackSet.empty() && selectedTrack >= 0)
                    trackSet.insert (selectedTrack);
                if (trackSet.erase (t) == 0)
                    trackSet.insert (t);
            }
            else
                trackSet.clear();
            selectedTrack = t;
            sceneSelected = false;
            titleSelected = true; // (round 190ef: Ctrl+R renames it)
            repaint();
            if (mixer != nullptr)
                mixer->repaint();
            notify (GridPick::select);
            return;
        }
        if (hit.scene >= 0 && hit.track < 0)
        {
            // round 190ef: a click on the scene selects it (Duplicate, Capture and Insert, Rename act on it); a drag on
            // one of its fields sets its tempo / time signature
            selectedScene = hit.scene;
            sceneSelected = true;
            titleSelected = false;
            selectedField = 0;
            if (const auto field = sceneFieldAt (e.getPosition(), hit.scene); field != 0)
            {
                selectedField = field; // (round 190em: a selected field; Del returns it to its default)
                beginSceneFieldDrag (hit.scene, field);
                repaint();
                return;
            }
            if (e.x < sceneX() + launchZone)
                launchScene (hit.scene);
            repaint();
            return;
        }
        if (hit.track < 0 || hit.scene < 0)
            return;
        // round 190lr (DL-29): Shift+click extends the selection to a rectangle; any other click starts a new one
        if (e.mods.isShiftDown() && ! sceneSelected && selectedTrack >= 0 && selectedScene >= 0)
        {
            if (! rangeLive())
                rangeTrack0 = selectedTrack, rangeScene0 = selectedScene;
            selectedTrack = hit.track;
            selectedScene = hit.scene;
            markRangeEnd();
            repaint();
            notify (GridPick::select);
            return;
        }
        clearRange();
        sceneSelected = titleSelected = false;
        selectedTrack = hit.track;
        selectedScene = hit.scene;
        repaint();
        notify (GridPick::select);
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        const auto filled = launcher::clipAt (*grid, hit.track, hit.scene) != nullptr;
        rubberBand = ! filled && e.x - columnX (hit.track) >= launchZone && e.mods.isLeftButtonDown(); // (round 190lr: from an empty slot)
        if (e.x - columnX (hit.track) < launchZone)
        {
            if (recordingScene (hit.track) == hit.scene)
                engine.endSlotRecording (hit.track, true); // the recording clip's launch button: on into loop playback
            else if (filled)
            {
                engine.launcherCommand ({ launcher::Command::Type::launch, hit.track, hit.scene });
                pressedTrack = hit.track;
                pressedScene = hit.scene;
            }
            // an armed track's empty slot: its record button; while the track records another slot, that take ends and
            // plays on and this slot records from there (round 190cy, as Live 12.4.16)
            else if (engine.isGridTrackArmed (hit.track))
                engine.recordSlot (hit.track, hit.scene);
            else
                engine.launcherCommand ({ launcher::Command::Type::stopTrack, hit.track }); // an empty slot's stop button
        }
        else if (filled)
        {
            dragTrack = hit.track; // (a drag from the clip's name moves it, with Ctrl copies it)
            dragScene = hit.scene;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (fieldScene >= 0)
        {
            dragSceneField (e);
            return;
        }
        if (rubberBand) // round 190lr (DL-29): the rectangle from the empty slot pressed to the slot under the pointer
        {
            const auto hit = hitTestCell (e.getPosition());
            if (hit.track >= 0 && hit.scene >= 0 && (hit.track != selectedTrack || hit.scene != selectedScene || ! rangeLive()))
            {
                if (! rangeLive())
                    rangeTrack0 = selectedTrack, rangeScene0 = selectedScene;
                selectedTrack = hit.track;
                selectedScene = hit.scene;
                markRangeEnd();
                repaint();
            }
            return;
        }
        if (dragTrack < 0 || e.getDistanceFromDragStart() < 5)
            return;
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
        if (container == nullptr || container->isDragAndDropActive())
            return;
        const juce::Rectangle<int> cell (columnX (dragTrack), rowY (dragScene), columnWidth, rowHeight);
        container->startDragging (juce::String (gridDragDescription) + "|" + juce::String (dragTrack) + "|" + juce::String (dragScene), this,
                                  juce::ScaledImage (createComponentSnapshot (cell)), false);
    }

    // --- drops: a clip of the grid, a pattern or a file from the Browser, files from outside (round 190bv) ---
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        int t = 0, r = 0;
        return clipGridDrag (details.description, t, r) || detailDeviceDrag (details.description, t, r) || playlistClipDrag (details.description, t)
               || BrowserComponent::isGeneratorDrag (details) || BrowserComponent::isEffectDrag (details) // (issue #152 point 6)
               || BrowserComponent::draggedPattern (details) >= 0
               || SampleLibrary::isSampleFile (BrowserComponent::draggedFile (details))
               || workflowextras::isMidiFile (BrowserComponent::draggedFile (details).getFullPathName()); // (round 190iu)
    }
    void itemDragEnter (const SourceDetails& details) override { itemDragMove (details); }
    void itemDragMove (const SourceDetails& details) override
    {
        int insert = 0, slot = 0;
        if (detailDeviceDrag (details.description, insert, slot) || BrowserComponent::isGeneratorDrag (details) || BrowserComponent::isEffectDrag (details))
        {
            setDeviceDropTrack (trackAtX (details.localPosition.x));
            return;
        }
        setDropCell (details.localPosition);
    }
    void itemDragExit (const SourceDetails&) override
    {
        setDropCell ({ -1, -1 });
        setDeviceDropTrack (-1);
    }
    void itemDropped (const SourceDetails& details) override
    {
        if (BrowserComponent::isGeneratorDrag (details) || BrowserComponent::isEffectDrag (details))
        {
            setDeviceDropTrack (-1);
            pluginDropped (details);
            return;
        }
        // round 190dr: a device from the Device View dropped on a track (anywhere in its column): it moves to the end of
        // that track's chain, with Ctrl a copy goes there (core/DetailChain.h moveEffectToInsert)
        if (int from = 0, slot = 0; detailDeviceDrag (details.description, from, slot))
        {
            const auto t = trackAtX (details.localPosition.x);
            setDeviceDropTrack (-1);
            if (t < 0)
                return;
            const auto to = sessionmixer::stripOf (engine.getSong(), t).insert;
            if (to <= 0)
            {
                ui::postHint ({ tr ("This track plays through no mixer track of its own: the device can't go there"), {} }, 3000);
                return;
            }
            if (to == from)
                return;
            if (to < (int) engine.getSong().mixer.size() && (int) engine.getSong().mixer[(std::size_t) to].effects.size() >= maxEffectSlots)
            {
                ui::postHint ({ tr ("This insert has no free slot (10 effects)"), {} }, 3000);
                return;
            }
            const auto copy = juce::ModifierKeys::currentModifiers.isCommandDown();
            engine.capturePluginStates();
            engine.editSongStructural ([=] (Song& s) { detailchain::moveEffectToInsert (s, from, slot, to, copy); });
            repaint();
            return;
        }
        const auto hit = hitTestCell (details.localPosition);
        setDropCell ({ -1, -1 });
        if (hit.track < 0 || hit.scene < 0)
            return;
        int fromTrack = 0, fromScene = 0;
        if (clipGridDrag (details.description, fromTrack, fromScene))
        {
            const auto copy = juce::ModifierKeys::currentModifiers.isCommandDown();
            engine.editSong ([=] (Song& s) { session::moveClip (s, fromTrack, fromScene, hit.track, hit.scene, copy); });
        }
        else if (int index = 0; playlistClipDrag (details.description, index))
        {
            // round 190ef: a Playlist clip dragged onto a slot: a copy of it there (the Playlist keeps it)
            engine.editSong ([=] (Song& s) { session::fromPlaylist (s, (std::size_t) index, hit.track, hit.scene, false); });
        }
        else if (const auto pattern = BrowserComponent::draggedPattern (details); pattern >= 0)
            putPattern (hit.track, hit.scene, pattern);
        else if (const auto file = BrowserComponent::draggedFile (details); file.existsAsFile() && workflowextras::isMidiFile (file.getFullPathName()))
            workflowextras::dropMidiOnSlot (engine, { file.getFullPathName() }, hit.track, hit.scene); // (round 190iu, issue #191.2)
        else if (file.existsAsFile())
            putAudioFile (file, hit.track, hit.scene);
        selectedTrack = hit.track;
        selectedScene = hit.scene;
        repaint();
        notify (GridPick::select);
    }
    // issue #152 point 6 (Live, DROP-FACTS C): an instrument or an audio effect from the Browser dropped on a track's
    // column goes into that track (an instrument replaces the track's, an effect goes to the end of its chain); on an
    // empty column (Live's drop area) the track becomes a MIDI / audio track holding it. Refusals show in the status bar.
    void pluginDropped (const SourceDetails& details)
    {
        auto t = trackAtX (details.localPosition.x);
        if (t < 0 && details.localPosition.x >= 0 && details.localPosition.x < cellArea().getRight() + columnWidth)
            t = trackmodes::firstFreeTrack (engine.getSong()); // (right of the last column: Live's drop area, a new track)
        if (t < 0)
            return;
        auto generator = BrowserComponent::draggedGenerator (details);
        auto fx = generator ? std::optional<EffectSlot>() : BrowserComponent::draggedEffect (details);
        if (! generator && ! fx)
            return;
        auto apply = [&] (Song& song) {
            return generator ? plugindrop::instrumentOnSessionTrack (song, *generator, t) : plugindrop::effectOnSessionTrack (song, *fx, t);
        };
        auto trial = engine.getSong(); // (a refused drop leaves no undo step)
        const auto check = apply (trial);
        if (! check.ok())
        {
            ui::postHint ({ plugindropui::message (check.error), {} }, 4000);
            return;
        }
        engine.editSong ([&] (Song& song) { apply (song); });
        selectedTrack = t;
        repaint();
        notify (GridPick::select);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return SampleLibrary::isSampleFile (juce::File (f)); })
               || workflowextras::anyMidiFile (files); // (round 190iu, issue #191.2: .mid files and plugins' MIDI drags)
    }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { setDropCell ({ x, y }); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { setDropCell ({ x, y }); }
    void fileDragExit (const juce::StringArray&) override { setDropCell ({ -1, -1 }); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        const auto hit = hitTestCell ({ x, y });
        setDropCell ({ -1, -1 });
        if (hit.track < 0 || hit.scene < 0)
            return;
        auto scene = hit.scene; // (several files: one below the other in the track)
        if (workflowextras::anyMidiFile (files)) // round 190iu (issue #191.2): MIDI files first, each in the next slot down
        {
            juce::StringArray midi;
            for (const auto& f : files)
                if (workflowextras::isMidiFile (f))
                    midi.add (f);
            workflowextras::dropMidiOnSlot (engine, midi, hit.track, scene);
            if (const auto* grid = gridOf(); grid != nullptr)
                while (scene < launcher::maxScenes && launcher::clipAt (*grid, hit.track, scene) != nullptr)
                    ++scene;
        }
        for (const auto& f : files)
            if (scene < launcher::maxScenes && ! workflowextras::isMidiFile (f) && putAudioFile (juce::File (f), hit.track, scene))
                ++scene;
        repaint();
    }

    // Round 190iv: the footer row of buttons (Stop all clips, Back to Playlist, Session Record, Grooves, Mixer, the launch
    // quantisation). In Live's look they sit in the control bar and the Main column; the row stays as an option.
    void setButtonRow (bool shown)
    {
        if (shown == buttonRow)
            return;
        buttonRow = shown;
        resized();
        repaint();
    }
    bool groovePoolShown() const noexcept { return groovePool != nullptr && groovePool->isVisible(); }
    void addMixerViewItems (juce::PopupMenu& menu)
    {
        if (mixer != nullptr)
            mixer->addViewItems (menu);
    }
    int selectedSceneNow() const noexcept { return selectedScene; }

    // The Groove Pool under the grid (made the first time it is shown).
    void showGroovePool (bool show)
    {
        if (show && groovePool == nullptr)
        {
            groovePool = makeGroovePool (engine);
            addChildComponent (*groovePool);
        }
        if (groovePool != nullptr)
            groovePool->setVisible (show);
        grooves.setToggleState (show, juce::dontSendNotification);
        resized();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (stopAllDown) // (round 190iv)
        {
            stopAllDown = false;
            repaint (stopAllBox().expanded (4));
        }
        fieldScene = -1;
        if (std::exchange (rubberBand, false) && rangeLive() && rangeTrack0 == selectedTrack && rangeScene0 == selectedScene)
            clearRange(); // (a click without a drag: one slot)
        if (pressedTrack >= 0)
            engine.launcherCommand ({ launcher::Command::Type::release, pressedTrack, pressedScene });
        pressedTrack = pressedScene = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto hit = hitTestCell (e.getPosition()); hit.scene >= 0 && hit.track < 0)
            if (const auto field = sceneFieldAt (e.getPosition(), hit.scene); field != 0)
            {
                typeSceneField (hit.scene, field); // (round 190ef: a double-click types the value)
                return;
            }
        if (const auto t = headerTrackAt (e.getPosition()); t >= 0) // (round 183a: the track's devices)
        {
            selectedTrack = t;
            notify (GridPick::openTrack);
            return;
        }
        const auto hit = hitTestCell (e.getPosition());
        if (hit.track >= 0 && hit.scene >= 0 && e.x - columnX (hit.track) >= launchZone)
            if (const auto* grid = gridOf(); grid != nullptr)
            {
                if (launcher::clipAt (*grid, hit.track, hit.scene) == nullptr && e.mods.isAltDown())
                    putPattern (hit.track, hit.scene, engine.getSong().currentPattern); // (owner L2: Alt keeps the old way)
                else if (launcher::clipAt (*grid, hit.track, hit.scene) == nullptr)
                {
                    // Live (owner decision L2, details DL-22, round 190ld): a double-click on an empty slot makes an empty
                    // MIDI clip and shows it in the Detail View; the slot menu keeps "Put a pattern here"
                    insertEmptyClip (hit.track, hit.scene);
                    if (const auto* now = gridOf(); now != nullptr && launcher::clipAt (*now, hit.track, hit.scene) != nullptr)
                    {
                        selectedTrack = hit.track;
                        selectedScene = hit.scene;
                        notify (GridPick::openClip);
                    }
                }
                else
                    notify (GridPick::openClip); // (round 183a: the clip in the Detail View)
            }
    }

    SessionMixerComponent* sessionMixer() noexcept { return mixer.get(); }
    void syncMixerForSnapshot() { syncMixer(); }
    void deviceDropForSnapshot (int t) { setDeviceDropTrack (t); }

    // The Detail View's listener (round 183a; none in Pattern workflow, where no grid exists).
    std::function<void (int, int, GridPick)> onPick;
    // Round 190ef: the scene fields' switch changed (the main window saves its UI settings).
    std::function<void()> onSceneFieldsChanged;
    std::pair<int, int> selection() const noexcept { return { selectedTrack, selectedScene }; }

    // Round 190ef (MIDI / Key Map Mode): the clip slots (a mapping belongs to the slot, not to its clip), each scene's
    // launch button, each track's clip stop button; Stop all clips and Session Record are tagged buttons.
    void mapSpots (std::vector<mapmode::Spot>& out) override
    {
        const auto cells = cellArea();
        const auto columns = columnCount(), scenes = rowCount();
        for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
        {
            for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
                out.push_back ({ juce::Rectangle<int> (columnX (t), rowY (r), columnWidth, rowHeight).getIntersection (cells),
                                 remotemap::Target::slot (t, r) });
            out.push_back ({ juce::Rectangle<int> (columnX (t), cells.getBottom(), launchZone, statusHeight), remotemap::Target::trackStop (t) });
        }
        const juce::Rectangle<int> sceneColumn (sceneX(), cells.getY(), sceneWidthNow(), cells.getHeight());
        for (int r = firstRow(); r < scenes && rowY (r) < cells.getBottom(); ++r)
            out.push_back ({ juce::Rectangle<int> (sceneColumn.getX(), rowY (r), sceneBaseWidthNow(), rowHeight).getIntersection (sceneColumn),
                             remotemap::Target::sceneLaunch (r) });
        // round 190kc: the Relative Session Mapping Strip
        if (mapStrip)
        {
            for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
                out.push_back ({ mapStripTrackBox (t).getIntersection (cells.withY (0).withHeight (getHeight())), remotemap::Target::selectedClip (t) });
            const RemoteAction main[] { RemoteAction::launchSelectedScene, RemoteAction::cancelLaunch, RemoteAction::sceneUp, RemoteAction::sceneDown };
            for (int k = 0; k < 4; ++k)
                out.push_back ({ mapStripMainBox (k), remotemap::Target::of (main[k]) });
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        juce::ignoreUnused (e);
        const auto dx = w.deltaX != 0.0f ? w.deltaX : (e.mods.isShiftDown() ? w.deltaY : 0.0f);
        const auto dy = e.mods.isShiftDown() ? 0.0f : w.deltaY;
        scrollX -= juce::roundToInt (dx * 240.0f);
        scrollY -= juce::roundToInt (dy * 120.0f);
        clampScroll();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto* grid = gridOf();
        if (grid == nullptr)
            return false;
        const auto code = key.getKeyCode();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            clearRange(); // (round 190lr)
            selectedTrack = stepTrack (selectedTrack, code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0); // (round qafix190kq: past the lanes)
            selectedScene = juce::jlimit (0, rowCount() - 1, selectedScene + (code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0));
            revealSelection();
            repaint();
            notify (GridPick::select);
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            if (key.getModifiers().isCommandDown())
                launchScene (selectedScene);
            else
                engine.launcherCommand ({ launcher::Command::Type::launch, selectedTrack, selectedScene });
            return true;
        }
        // round 190em: Del on a selected scene field is the reference's "Return to Default" (live check 190ei)
        if (code == juce::KeyPress::deleteKey && sceneSelected && selectedField != 0 && sceneFieldsOn)
        {
            returnSceneFieldToDefault (selectedScene, selectedField);
            return true;
        }
        if (code == juce::KeyPress::deleteKey && sceneSelected) // round 190ef: Delete on a selected scene deletes it
        {
            deleteScene (selectedScene);
            return true;
        }
        if (code == juce::KeyPress::deleteKey)
        {
            if (rangeLive()) // round 190lr (DL-29): every slot of the rectangle, one undo step
            {
                const auto t0 = juce::jmin (rangeTrack0, selectedTrack), t1 = juce::jmax (rangeTrack0, selectedTrack);
                const auto r0 = juce::jmin (rangeScene0, selectedScene), r1 = juce::jmax (rangeScene0, selectedScene);
                engine.editSong ([=] (Song& s) {
                    for (int t = t0; t <= t1; ++t)
                        for (int r = r0; r <= r1; ++r)
                            launcher::clearClip (s, t, r);
                });
                repaint();
                return true;
            }
            const int t = selectedTrack, r = selectedScene;
            engine.editSong ([t, r] (Song& s) { launcher::clearClip (s, t, r); });
            repaint();
            return true;
        }
        // round 190ff: the reference's Freeze Track key (Ctrl+Shift+Alt+F) toggles the selected track's freeze
        if (key.getModifiers().isCommandDown() && key.getModifiers().isAltDown() && key.getModifiers().isShiftDown()
            && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code) == 'F' && sessionkeys::defaultKeyActs (sessionkeys::Command::freezeTrack))
        {
            toggleTrackFreeze (selectedTrack);
            return true;
        }
        // round 190im (R12, DL-15): 0 deactivates / activates the selected clip (Live's "Deactivate Selected Clip 0"), only
        // in the Clip-launch layout (core/SessionKeys.h); elsewhere the key goes on to the key map
        if (code == '0' && ! key.getModifiers().isAnyModifierKeyDown() && ! sceneSelected && ! titleSelected
            && sessionkeys::defaultKeyActs (sessionkeys::Command::gridDeactivateClip) && launcher::clipAt (*grid, selectedTrack, selectedScene) != nullptr)
        {
            toggleClipActive (selectedTrack, selectedScene);
            return true;
        }
        // round gap190og (detail-020): Live's Crop Clip key (Ctrl+Shift+J) on the selected MIDI clip, Clip-launch layout only
        if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && ! key.getModifiers().isAltDown() && ! sceneSelected && ! titleSelected
            && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code) == 'J' && sessionkeys::defaultKeyActs (sessionkeys::Command::gridCropClip)
            && canCropClip (selectedTrack, selectedScene))
        {
            cropClip (selectedTrack, selectedScene);
            return true;
        }
        // round 190bv: the clipboard, duplicate and a new empty MIDI clip
        if (key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown())
        {
            const auto shift = key.getModifiers().isShiftDown();
            const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
            if (! shift && (letter == 'C' || letter == 'X'))
            {
                copySlot (selectedTrack, selectedScene, letter == 'X');
                return true;
            }
            if (! shift && letter == 'V')
            {
                pasteSlot (selectedTrack, selectedScene);
                return true;
            }
            if (! shift && letter == 'D')
            {
                if (sceneSelected) // round 190ef: the selected scene's Duplicate
                    duplicateScene (selectedScene);
                else
                    duplicateSlot (selectedTrack, selectedScene);
                return true;
            }
            // round 190ef: the reference's Insert Scene (Ctrl+I), Capture and Insert Scene (Ctrl+Shift+I) and Rename
            // (Ctrl+R), only in the Clip-launch layout (elsewhere the chords stay LYNX's global keys, core/SessionKeys.h)
            if (letter == 'I' && sessionkeys::defaultKeyActs (shift ? sessionkeys::Command::gridCaptureInsertScene : sessionkeys::Command::gridInsertScene))
            {
                if (shift)
                    captureAndInsertScene (selectedScene);
                else
                    insertSceneBelow (selectedScene);
                return true;
            }
            if (! shift && letter == 'R' && sessionkeys::defaultKeyActs (sessionkeys::Command::gridRename) && (sceneSelected || titleSelected))
            {
                if (sceneSelected)
                    renameScene (selectedScene);
                else
                    renameTrack (selectedTrack);
                return true;
            }
            // round 190kn (DL-24): Ctrl+R on a selected clip renames the clip (Live's Edit › Rename)
            if (! shift && letter == 'R' && sessionkeys::defaultKeyActs (sessionkeys::Command::gridRename) && gridOf() != nullptr
                && launcher::clipAt (*gridOf(), selectedTrack, selectedScene) != nullptr)
            {
                renameClip (selectedTrack, selectedScene);
                return true;
            }
            // (round 190cm: only in the Clip-launch layout; elsewhere Ctrl+Shift+M stays "Export a MIDI file")
            if (shift && letter == 'M' && sessionkeys::defaultKeyActs (sessionkeys::Command::gridNewMidiClip))
            {
                insertEmptyClip (selectedTrack, selectedScene);
                return true;
            }
        }
        return false;
    }

    // Round 190im (R6, DL-21): what the part under the mouse is and does, in the hint bar (Live's Info View names
    // whatever is hovered: launch button, stop button, track status, scene, slot). Event-driven: the hint bar asks on a
    // mouse move and repaints only when the text changes.
    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint hint;
        const auto* grid = gridOf();
        if (grid == nullptr)
            return hint;
        const auto& song = engine.getSong();
        const auto cells = cellArea();
        if (const auto t = headerTrackAt (p); t >= 0 && p.x >= columnX (t) + columnWidth - 22 && trackTaken (t)) // (round 190lr)
        {
            hint.text = tr ("Back to Arrangement: [LMB] this track plays its Playlist clips again");
            return hint;
        }
        if (const auto t = headerTrackAt (p); t >= 0)
        {
            hint.text = tr ("Track title: [LMB] selects the track  [RMB] track menu  [Ctrl][R] renames it");
            hint.value = t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty()
                             ? utf8 (song.playlistTracks[(std::size_t) t].name) : tr ("Track {0}", { juce::String (t + 1) });
            hint.hasMenu = true;
            return hint;
        }
        if (const auto t = statusTrackAt (p); t >= 0)
        {
            const auto x = p.x - columnX (t);
            if (x >= columnWidth - armWidth - 6 && ! mixer->armsShown())
                hint.text = tr ("Arm Session Recording: [LMB] arms this track (the others disarm)  [Ctrl][LMB] keeps the others armed");
            else if (x < launchZone)
                hint.text = tr ("Clip Stop button: [LMB] stops the clip playing on this track (at the launch quantisation)");
            else
                hint.text = tr ("Track status: the playing clip's loop count, position and length (a one-shot clip: the time left)");
            return hint;
        }
        const auto hit = hitTestCell (p);
        if (hit.scene >= 0 && hit.track < 0 && p.x >= sceneX())
        {
            const auto& scenes = grid->scenes;
            hint.text = p.x < sceneX() + launchZone
                            ? tr ("Scene Launch button: [LMB] launches every clip in this row; the next scene is selected  [Ctrl][Enter] launches the selected scene")
                            : tr ("Scene: [LMB] selects it  [RMB] scene menu (insert, duplicate, rename, tempo)  [Ctrl][R] renames it");
            hint.value = hit.scene < (int) scenes.size() && ! scenes[(std::size_t) hit.scene].name.empty()
                             ? utf8 (scenes[(std::size_t) hit.scene].name) : juce::String (hit.scene + 1);
            hint.hasMenu = true;
            return hint;
        }
        if (hit.scene >= 0 && hit.track >= 0)
        {
            const auto* clip = launcher::clipAt (*grid, hit.track, hit.scene);
            const auto onButton = p.x - columnX (hit.track) < launchZone;
            if (clip == nullptr)
                hint.text = engine.isGridTrackArmed (hit.track) ? tr ("Clip Record button: [LMB] records a new clip into this slot")
                            : onButton ? tr ("Clip Stop button: [LMB] stops the clip playing on this track (at the launch quantisation)")
                                       : tr ("Empty slot: drop a clip, a pattern or a sample here  [2xLMB] a new empty MIDI clip  [Alt][2xLMB] the current pattern  [RMB] slot menu");
            else
            {
                hint.text = onButton ? tr ("Clip Launch button: [LMB] launches the clip (at the launch quantisation)  [Enter] launches the selected clip")
                                     : tr ("Clip: [LMB] selects it, drag moves it ([Ctrl] copies)  [2xLMB] opens it  [RMB] clip menu");
                if (! onButton && sessionkeys::defaultKeyActs (sessionkeys::Command::gridDeactivateClip)) // (round 190im, R12)
                    hint.text << "  " << tr ("[0] deactivates / activates it");
                hint.value = clipName (song, *grid, *clip);
                if (clip->gridSlot >= 0 && clip->gridSlot < (int) grid->slots.size())
                {
                    const auto& slot = grid->slots[(std::size_t) clip->gridSlot];
                    if (slot.follow.enabled && (slot.follow.a != FollowKind::none || slot.follow.b != FollowKind::none))
                        hint.value << "  " << tr ("(Follow Action)");
                }
            }
            hint.hasMenu = true;
        }
        return hint;
    }

private:
    // round 190im (R12): one undo step; a deactivated clip that plays goes on until it is launched again (Live)
    void toggleClipActive (int track, int scene)
    {
        editSlot (track, scene, [] (ClipGridSlot& s) { s.active = ! s.active; });
        repaint();
    }

    // Round 190im (R6): a scene launched from here (its launch button, Ctrl+Enter): its button pulses until its clips
    // start (DL-5: Live's queued pulse, black ↔ #00D38D every 400 ms, filmed by round 190ig), and the scene below becomes
    // the selected one (DL-6, Live's manual: "The scene below a launched scene will automatically be selected as the
    // next to be launched"), so Ctrl+Enter steps down the scenes.
    void launchScene (int scene)
    {
        engine.launcherCommand ({ launcher::Command::Type::launchScene, 0, scene });
        if (queuedScene >= 0 && queuedScene != scene)
            repaintScene (queuedScene);
        queuedScene = scene;
        queuedSeen = false;
        queuedTicks = 0;
        queuedBlink = blinkPhaseNow();
        // Live's "Trigger Recording on Launch": the armed tracks record into this scene's empty slots
        if (recordOnSceneLaunch && ! engine.isSessionRecording())
            engine.toggleSessionRecord (scene);
        if (selectNextOnLaunch && scene + 1 < rowCount())
        {
            selectedScene = scene + 1;
            sceneSelected = true;
            titleSelected = false;
            selectedField = 0;
            revealSelection();
        }
        repaint();
    }
    void repaintScene (int row)
    {
        const auto cells = cellArea();
        if (row >= 0)
            repaint (juce::Rectangle<int> (sceneX(), rowY (row), sceneWidthNow(), rowHeight).getIntersection (cells.withRight (getWidth())));
    }
    int queuedScene = -1;       // the scene launched here, until its clips start (−1: none)
    bool queuedSeen = false;    // (one of its clips was seen waiting)
    int queuedTicks = 0;        // (grid timer ticks since the launch)
    std::uint32_t queuedBlink = 0;

    AudioEngine& engine;
    juce::TextButton stopAll, backToPlaylist, grooves, mixerButton;
    std::unique_ptr<SessionMixerComponent> mixer;
    RingButton sessionRecord;
    juce::ComboBox globalQuant;
    bool buttonRow = true, stopAllDown = false; // (round 190iv)
public:
    // Round 190kc: Live's Relative Session Mapping Strip, under the status row while a map mode is on [Live manual 34.2.4.1:
    // "In both MIDI Map Mode and Key Map Mode, a strip of assignable controls appears below the Session grid"]: per track a
    // button that launches its clip at the highlighted scene; in the Main column Scene Up / Down, Launch and Cancel Launch.
    void setMapStrip (bool on)
    {
        if (mapStrip == on)
            return;
        mapStrip = on;
        resized();
        repaint();
    }
private:
    bool mapStrip = false;
    // Live 12.4 (shots/ref/live-keymap-strip.png, the Patience set in Key Map Mode): a 17 px row and its 2 px separator under
    // the status row; per track a button over the column (x 2..71 of a 76 px column, y 1..15) with a ▶; in the Main column
    // Scene Launch ▶ (x 4, 13 px), Cancel Scene Launch ■ (x 19, 17 px), Scene Up / Down (x 38 / 55, 15 px round wells with
    // an arrow) and the scene number box (x 72, 14 px). Only shown in a map mode, so always under the mode's tint.
    static constexpr int mapStripHeight = 19;
    int mapStripNow() const noexcept { return mapStrip ? mapStripHeight : 0; }
    juce::Rectangle<int> mapStripRow() const { const auto cells = cellArea(); return { 0, cells.getBottom() + statusHeight, getWidth(), mapStripNow() }; }
    // the strip's parts: a track's Track Launch; in the Main column 0 Scene Launch, 1 Cancel Scene Launch, 2 Scene Up,
    // 3 Scene Down, 4 the scene number box
    juce::Rectangle<int> mapStripTrackBox (int track) const
    {
        const auto row = mapStripRow();
        return { columnX (track) + 2, row.getY() + 1, juce::jmax (4, (int) columnWidth - 6), 15 };
    }
    juce::Rectangle<int> mapStripMainBox (int which) const
    {
        const auto row = mapStripRow();
        constexpr int x[] { 4, 19, 38, 55, 72 }, w[] { 13, 17, 15, 15, 14 };
        which = juce::jlimit (0, 4, which);
        return { sceneX() + x[which], row.getY() + 2, w[which], 15 };
    }
    void paintMapStrip (juce::Graphics& g) const
    {
        if (! mapStrip)
            return;
        namespace c = theme::colour;
        namespace lv = theme::livelook;
        const auto live = liveLook();
        const auto row = mapStripRow();
        g.setColour (live ? lv::separator : c::border);
        g.fillRect (row);
        g.setColour (live ? lv::surface : c::bg2);
        g.fillRect (row.withHeight (row.getHeight() - 2));
        const auto ink = live ? lv::text : c::textSecondary;
        const auto box = [&] (juce::Rectangle<int> r, bool round) {
            if (round)
            {
                g.setColour (live ? lv::knobRing : c::border);
                g.fillEllipse (r.toFloat());
                g.setColour (live ? lv::well : c::bg4);
                g.fillEllipse (r.toFloat().reduced (1.0f));
                return;
            }
            g.setColour (live ? lv::knobRing : c::border);
            g.fillRect (r);
            g.setColour (live ? lv::well : c::bg4);
            g.fillRect (r.reduced (1));
        };
        const auto play = [&] (juce::Rectangle<int> r) {
            g.setColour (ink);
            const auto cx = r.getCentreX() - 3, cy = r.getCentreY();
            for (int k = 0; k < 7; ++k) // (a ▶ in whole-pixel columns)
                g.fillRect (cx + k, cy - (6 - k) / 2, 1, 2 * ((6 - k) / 2) + 1);
        };
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (row.withRight (cellArea().getRight()));
            for (int t = firstColumn(); t < columnCount() && columnX (t) < cellArea().getRight(); t = nextShown (t))
            {
                const auto r = mapStripTrackBox (t);
                box (r, false);
                play (r);
            }
        }
        box (mapStripMainBox (0), false);
        play (mapStripMainBox (0));
        box (mapStripMainBox (1), false);
        g.setColour (ink);
        g.fillRect (mapStripMainBox (1).withSizeKeepingCentre (9, 9));
        for (int k = 2; k <= 3; ++k)
        {
            const auto r = mapStripMainBox (k);
            box (r, true);
            g.setColour (ink);
            const auto cx = r.getCentreX(), cy = r.getCentreY();
            g.fillRect (cx, cy - 3, 1, 7); // (the arrow's stem and head)
            const auto dir = k == 2 ? -1 : 1;
            for (int d = 1; d <= 2; ++d)
                g.fillRect (cx - d, cy + dir * (3 - d), 2 * d + 1, 1);
        }
        const auto number = mapStripMainBox (4);
        box (number, false);
        g.setColour (ink);
        g.setFont (liveFont());
        textcache::drawText (g, juce::String (selectedScene + 1), number, juce::Justification::centred, false);
    }
public:
    std::unique_ptr<juce::Component> groovePool;
    int scrollX = 0, scrollY = 0;
    int selectedTrack = 0, selectedScene = 0;
    int pressedTrack = -1, pressedScene = -1;
    int dragTrack = -1, dragScene = -1; // the clip a drag would take
    int dropTrack = -1, dropScene = -1; // the slot a drag is over
    int deviceDropTrack = -1;           // round 190dr: the track a dragged device is over
    // round 190ef: a scene (its cell clicked) or a column's title is the selection Duplicate / Rename act on
    bool sceneSelected = false, titleSelected = false;
    // round 190ef: a scene field being dragged (scene −1: none; field 1 tempo, 2 the time signature's numerator, 3 its
    // denominator: separate drag targets, as the reference's "n / d")
    int fieldScene = -1, fieldKind = 0;
    double fieldStart = 0.0;
    int fieldOtherPart = 4;     // the signature part not dragged
    bool fieldUndoTaken = false; // (one undo step per drag)
    int selectedField = 0;       // round 190em: the field of the selected scene clicked last (0: none)

    struct Cell
    {
        int track = -1, scene = -1;
    };

    // The project's grid, or an empty one to draw: a project gets a real one with its first clip, so that looking at the
    // grid does not change the project.
    const ClipGrid* gridOf() const noexcept
    {
        static const ClipGrid none;
        const auto& g = engine.getSong().clipGrid;
        return g ? &*g : &none;
    }
    int rowCount() const noexcept { return std::max (minRows, (int) gridOf()->scenes.size()); }
    // Round qafix190kq (QA-2): the grid's columns skip take lanes (comp::isTakeLane: Live shows them only in the
    // Arrangement, under their track; an imported set's lanes had pushed its tracks off the grid). `t` stays a Playlist
    // track everywhere; a column is a place on screen: columnTracks[c] is the track of column c, trackColumns[t] the column
    // of track t (−1: a lane, no column). Worked out again when the song changes (its stamp) or the grid's clips do.
    struct ColumnMap
    {
        std::vector<int> columnTracks, trackColumns;
        std::uint64_t stamp = ~std::uint64_t (0);
        std::size_t tracks = 0, clips = 0;
        const ClipGrid* grid = nullptr;
        bool showHidden = false; // (round 190op: PlaylistComponent::showHiddenTracks when it was worked out)
    };
    mutable ColumnMap columns_;
    const ColumnMap& columnMap() const
    {
        const auto& song = engine.getSong();
        const auto* grid = gridOf();
        auto& m = columns_;
        if (m.stamp == engine.getSongStamp() && m.tracks == song.playlistTracks.size() && m.grid == grid && m.clips == grid->clips.size()
            && m.showHidden == PlaylistComponent::showHiddenTracks)
            return m;
        m.showHidden = PlaylistComponent::showHiddenTracks;
        m.stamp = engine.getSongStamp();
        m.tracks = song.playlistTracks.size();
        m.grid = grid;
        m.clips = grid->clips.size();
        auto bound = (int) song.playlistTracks.size();
        std::vector<char> holdsClips ((std::size_t) launcher::maxTracks, 0);
        for (const auto& c : grid->clips)
            if (c.track >= 0 && c.track < launcher::maxTracks)
            {
                bound = std::max (bound, c.track + 1);
                holdsClips[(std::size_t) c.track] = 1;
            }
        m.columnTracks.clear();
        m.trackColumns.clear();
        for (int t = 0; t < launcher::maxTracks && (t < bound || (int) m.columnTracks.size() < minColumns); ++t)
        {
            // (a lane that holds grid clips keeps its column: nothing in the grid is ever out of reach)
            // (round 190op, P4: a hidden track has no column either, until Show hidden tracks; it still plays)
            const auto lane = (! holdsClips[(std::size_t) t] && comp::isTakeLane (song, t))
                              || (! PlaylistComponent::showHiddenTracks && trackstates::isHidden (song, t));
            m.trackColumns.push_back (lane ? -1 : (int) m.columnTracks.size());
            if (! lane)
                m.columnTracks.push_back (t);
        }
        return m;
    }
    // the tracks the grid covers, [0, columnCount()) (lanes included: they have no column)
    int columnCount() const { return (int) columnMap().trackColumns.size(); }
    // the columns on screen (the tracks without the lanes)
    int shownColumns() const { return (int) columnMap().columnTracks.size(); }
    int trackOfColumn (int c) const
    {
        const auto& m = columnMap();
        return c >= 0 && c < (int) m.columnTracks.size() ? m.columnTracks[(std::size_t) c] : -1;
    }
    int columnOfTrack (int t) const
    {
        const auto& m = columnMap();
        return t >= 0 && t < (int) m.trackColumns.size() ? m.trackColumns[(std::size_t) t] : -1;
    }
    // the shown track next to `t` (step −1 / +1), or `t` at an end
    int stepTrack (int t, int step) const
    {
        const auto c = columnOfTrack (t);
        if (c < 0)
            return trackOfColumn (0) >= 0 ? trackOfColumn (0) : t;
        const auto next = trackOfColumn (juce::jlimit (0, shownColumns() - 1, c + step));
        return next >= 0 ? next : t;
    }
    int footerNow() const noexcept { return buttonRow ? footerHeight : 0; }
    // (round 190iv) Stop All Clips in the scene column's status cell while the button row is hidden
    juce::Rectangle<int> stopAllBox() const
    {
        const auto cells = cellArea();
        return { sceneX() + 6, cells.getBottom() + 3, statusHeight - 6, statusHeight - 6 };
    }
    // Round 190kc: Live's return tracks as columns between the tracks and the Main column (no slots; their strips are in
    // the Session mixer under them), as View › Mixer Controls › Return Tracks shows them
    int returnColumnsNow() const { return SessionMixerComponent::returnColumnsFor (engine.getSong()); }
    int returnsWidthNow() const { return returnColumnsNow() * columnWidth; }
    int sceneX() const { return cellArea().getRight() + returnsWidthNow(); }
    juce::Rectangle<int> cellArea() const
    {
        return { 0, headerHeight, juce::jmax (0, getWidth() - sceneWidthNow() - returnsWidthNow()),
                 juce::jmax (0, getHeight() - headerHeight - footerNow() - statusHeight - mapStripNow() - poolHeight() - mixerHeight()) };
    }
    // the Session mixer's height (round 190dr): its shown sections, leaving the slots at least two rows
    int mixerHeight() const
    {
        return mixer == nullptr ? 0 : mixer->wantedHeight (getHeight() - headerHeight - footerNow() - statusHeight - mapStripNow() - poolHeight() - 2 * rowHeight);
    }
    void syncMixer()
    {
        if (mixer != nullptr)
            mixer->setColumns (shownColumns(), columnWidth, scrollX, cellArea().getRight(), sceneWidthNow(), returnColumnsNow(), columnMap().columnTracks);
    }
    int poolHeight() const
    {
        if (groovePool == nullptr || ! groovePool->isVisible())
            return 0;
        return juce::jmin (groovePoolHeight (engine), juce::jmax (0, getHeight() - headerHeight - footerNow() - statusHeight - 3 * rowHeight));
    }
    // the track whose status cell (stop and arm buttons) is at p, or -1
    int statusTrackAt (juce::Point<int> p) const
    {
        const auto cells = cellArea();
        if (p.y < cells.getBottom() || p.y >= cells.getBottom() + statusHeight || p.x < 0 || p.x >= cells.getRight())
            return -1;
        return trackOfColumn ((p.x + scrollX) / columnWidth);
    }
    // the scene the track records into, or waits to (-1: none)
    int recordingScene (int track) const
    {
        const auto* state = engine.launcherState();
        if (state == nullptr || track < 0 || track >= launcher::maxTracks)
            return -1;
        const auto& view = state->tracks[(std::size_t) track];
        const auto recording = view.recording.load (std::memory_order_relaxed);
        return recording >= 0 ? recording : view.recordPending.load (std::memory_order_relaxed);
    }
    void setDropCell (juce::Point<int> p)
    {
        const auto hit = hitTestCell (p);
        const auto t = hit.track >= 0 && hit.scene >= 0 ? hit.track : -1, r = t >= 0 ? hit.scene : -1;
        if (t != dropTrack || r != dropScene)
        {
            dropTrack = t;
            dropScene = r;
            repaint();
        }
    }
    // the left of track t's column (round qafix190kq: a lane, which has none, far left of the screen)
    int columnX (int t) const
    {
        const auto c = columnOfTrack (t);
        return c >= 0 ? c * columnWidth - scrollX : (t >= 0 && t >= columnCount() ? (shownColumns() + t - columnCount()) * columnWidth - scrollX : -(1 << 24));
    }
    int xOfColumn (int c) const noexcept { return c * columnWidth - scrollX; }
    // the track whose column is at x (any height), or -1
    int trackAtX (int x) const
    {
        if (x < 0 || x >= cellArea().getRight())
            return -1;
        return trackOfColumn ((x + scrollX) / columnWidth);
    }
    void setDeviceDropTrack (int t)
    {
        if (t != deviceDropTrack)
        {
            deviceDropTrack = t;
            repaint();
        }
    }
    int rowY (int r) const noexcept { return headerHeight + r * rowHeight - scrollY; }
    // the first track shown at the grid's left edge, and the shown track after `t` (round qafix190kq: lanes skipped;
    // columnCount() past the last)
    int firstColumn() const
    {
        const auto t = trackOfColumn (juce::jmax (0, scrollX / columnWidth));
        return t >= 0 ? t : columnCount();
    }
    int nextShown (int t) const
    {
        const auto c = columnOfTrack (t);
        if (c < 0)
            return t + 1;
        const auto next = trackOfColumn (c + 1);
        return next >= 0 ? next : columnCount();
    }
    int firstRow() const noexcept { return juce::jmax (0, scrollY / rowHeight); }

    void clampScroll()
    {
        const auto cells = cellArea();
        scrollX = juce::jlimit (0, juce::jmax (0, shownColumns() * columnWidth - cells.getWidth()), scrollX);
        scrollY = juce::jlimit (0, juce::jmax (0, rowCount() * rowHeight - cells.getHeight()), scrollY);
        syncMixer();
    }
    void revealSelection()
    {
        const auto cells = cellArea();
        if (const auto c = columnOfTrack (selectedTrack); c < 0)
            ; // (a lane: no column to reveal)
        else if (xOfColumn (c) < 0)
            scrollX = c * columnWidth;
        else if (xOfColumn (c) + columnWidth > cells.getRight())
            scrollX = (c + 1) * columnWidth - cells.getWidth();
        if (rowY (selectedScene) < headerHeight)
            scrollY = selectedScene * rowHeight;
        else if (rowY (selectedScene) + rowHeight > cells.getBottom())
            scrollY = (selectedScene + 1) * rowHeight - cells.getHeight();
        clampScroll();
    }
    void notify (GridPick pick)
    {
        if (onPick != nullptr)
            onPick (selectedTrack, selectedScene, pick);
    }
    // the track whose column title is at p, or -1
    // round 190lr (DL-109): the tracks selected together (empty: only selectedTrack)
    std::set<int> trackSet;
    std::vector<int> selectedTracks() const
    {
        if (trackSet.size() < 2)
            return { selectedTrack };
        return { trackSet.begin(), trackSet.end() };
    }
    void selectTracksForSnapshot (std::vector<int> tracks)
    {
        trackSet = { tracks.begin(), tracks.end() };
        if (! tracks.empty())
            selectedTrack = tracks.back();
        repaint();
    }
    bool trackTaken (int t) const
    {
        const auto* state = engine.launcherState();
        return state != nullptr && t >= 0 && t < launcher::maxTracks && state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
    }
    int headerTrackAt (juce::Point<int> p) const
    {
        if (p.y < 0 || p.y >= headerHeight || p.x < 0 || p.x >= cellArea().getRight())
            return -1;
        return trackOfColumn ((p.x + scrollX) / columnWidth);
    }
    Cell hitTestCell (juce::Point<int> p) const
    {
        Cell out;
        const auto cells = cellArea();
        if (p.y < headerHeight || p.y >= cells.getBottom())
            return out;
        const auto r = (p.y - headerHeight + scrollY) / rowHeight;
        if (r >= 0 && r < rowCount())
            out.scene = r;
        if (p.x >= cells.getRight() && p.x < sceneX()) // (round 190kc: the return columns hold no slots)
            out.scene = -1;
        if (p.x < cells.getRight())
            if (const auto t = trackOfColumn ((p.x + scrollX) / columnWidth); t >= 0)
                out.track = t;
        return out;
    }

    juce::String clipName (const Song& song, const ClipGrid& grid, const Clip& clip) const
    {
        const auto& slot = grid.slots[(std::size_t) clip.gridSlot];
        if (! slot.name.empty())
            return utf8 (slot.name);
        if (slot.blankName && layoutstyle::current() == layoutstyle::Style::clipLaunch) // (round 190em: Live's unnamed clip)
            return {};
        if (clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size())
            return utf8 (song.patterns[(std::size_t) clip.pattern].name);
        if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song.channels.size())
            return utf8 (song.channels[(std::size_t) clip.channel].name);
        return {};
    }

    // Round 190op (P4): a deactivated track's title dimmed with the power tag; a hidden track shown again, dimmed
    void paintTrackStateMark (juce::Graphics& g, const Song& song, int t, juce::Rectangle<int> r) const
    {
        const auto off = trackstates::trackDeactivated (song, trackstates::unitsOfTrack (song, t));
        const auto hidden = PlaylistComponent::showHiddenTracks && trackstates::isHidden (song, t);
        if (! off && ! hidden)
            return;
        g.setColour (theme::colour::bg0.withAlpha (0.45f));
        g.fillRect (r);
        if (off && r.getHeight() >= 12)
        {
            const auto size = (float) juce::jmin (13, r.getHeight() - 4);
            ui::drawPluginOffTag (g, { (float) r.getRight() - size - 4.0f, (float) r.getY() + ((float) r.getHeight() - size) * 0.5f, size + 1.0f, size });
        }
    }

    void paintHeader (juce::Graphics& g, const Song& song, const launcher::Runtime* state, int t, juce::Rectangle<int> r) const
    {
        if (liveLook())
        {
            paintHeaderLive (g, song, state, t, r);
            return;
        }
        namespace c = theme::colour;
        const auto* info = t < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) t] : nullptr;
        const auto taken = state != nullptr && state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
        g.setColour (c::bg3);
        g.fillRect (r);
        if (info != nullptr && info->colour != 0)
        {
            g.setColour (juce::Colour (info->colour));
            g.fillRect (r.getX(), r.getBottom() - 3, r.getWidth(), 3);
        }
        g.setColour (c::border);
        g.fillRect (r.getRight() - 1, r.getY(), 1, r.getHeight());
        g.setColour (c::textPrimary);
        g.setFont (theme::font (theme::type::small));
        const auto name = info != nullptr && ! info->name.empty() ? utf8 (info->name) : tr ("Track {0}", { juce::String (t + 1) });
        // Freeze (round 190ay): the track's instrument is frozen — the snowflake tag, the name dimmed
        const auto frozen = frozenChannelOf (song, t) >= 0;
        if (frozen)
        {
            g.setColour (c::textSecondary);
            theme::drawFrozenTag (g, juce::Rectangle<float> ((float) r.getRight() - (taken ? 40.0f : 20.0f), (float) r.getCentreY() - 6.5f, 14.0f, 13.0f),
                                  c::info.withAlpha (0.85f), c::bg1);
        }
        textcache::drawText (g, name, r.reduced (6, 0).withTrimmedRight ((taken ? 18 : 0) + (frozen ? 18 : 0)), juce::Justification::centredLeft, true);
        if (taken)
        {
            // the Playlist's clips on this track are silent: a struck-out timeline mark
            const auto mark = r.removeFromRight (22).withSizeKeepingCentre (14, 12).toFloat();
            g.setColour (c::warning);
            g.drawRect (mark, 1.0f);
            g.drawLine (mark.getX(), mark.getBottom(), mark.getRight(), mark.getY(), 1.4f);
        }
    }

    void paintSlot (juce::Graphics& g, const Song& song, const ClipGrid& grid, const launcher::Runtime* state, int t, int row, juce::Rectangle<int> r) const
    {
        if (liveLook())
        {
            paintSlotLive (g, song, grid, state, t, row, r);
            return;
        }
        namespace c = theme::colour;
        const auto* clip = launcher::clipAt (grid, t, row);
        const auto cell = r.reduced (1).withTrimmedBottom (0);
        const auto zone = cell.withWidth (launchZone).toFloat();
        const auto playing = state != nullptr && state->tracks[(std::size_t) t].playing.load (std::memory_order_relaxed) == row;
        const auto pending = state != nullptr && state->tracks[(std::size_t) t].pending.load (std::memory_order_relaxed) == row;
        const auto* view = state != nullptr ? &state->tracks[(std::size_t) t] : nullptr;
        const auto recording = view != nullptr && view->recording.load (std::memory_order_relaxed) == row;

        if (recording)
        {
            // a slot that records shows the new clip at once (round 190cy, Live 12.4.16 live check 190cw): its name, the
            // track's colour and a red launch button; pressing it sends the clip on into loop playback
            const auto trackColour = t < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) t].colour : 0u;
            const auto fill = clip != nullptr ? (grid.slots[(std::size_t) clip->gridSlot].colour != 0 ? juce::Colour (grid.slots[(std::size_t) clip->gridSlot].colour)
                                                                                                      : theme::tame (PlaylistComponent::patternColour (song, clip->pattern)))
                              : trackColour != 0 ? juce::Colour (trackColour)
                                                 : theme::tame (PlaylistComponent::patternColour (song, (int) song.patterns.size()));
            g.setColour (fill);
            g.fillRect (cell);
            const auto ink = fill.getPerceivedBrightness() > 0.62f ? c::bg1 : juce::Colours::white;
            theme::fillLaunchTriangle (g, zone.withSizeKeepingCentre (9.0f, 11.0f), c::record);
            g.setColour (ink);
            g.setFont (theme::font (theme::type::small));
            // (an audio take is named after its file, known only when it closes)
            textcache::drawText (g, clip != nullptr ? clipName (song, grid, *clip)
                        : engine.gridTrackInput (t) == AudioEngine::GridInput::midi ? utf8 (engine.slotRecordingName (t)) : juce::String(),
                        cell.withTrimmedLeft (launchZone).withTrimmedRight (3), juce::Justification::centredLeft, true);
        }
        else if (clip == nullptr)
        {
            const auto waiting = view != nullptr && view->recordPending.load (std::memory_order_relaxed) == row;
            g.setColour (c::bg2);
            g.fillRect (cell);
            if (waiting)
            {
                // a slot that waits for its recording to start: its launch button blinks in the record colour
                const auto blinkOn = (juce::Time::getMillisecondCounter() / classicBlinkPhaseMs) % 2 == 0;
                theme::fillLaunchTriangle (g, zone.withSizeKeepingCentre (9.0f, 11.0f), blinkOn ? c::record : c::record.withAlpha (0.3f));
            }
            else if (engine.isGridTrackArmed (t))
            {
                // an armed track's empty slots: round record buttons in place of the stop squares
                g.setColour (c::textSecondary.withAlpha (0.8f));
                g.fillEllipse (zone.withSizeKeepingCentre (8.0f, 8.0f));
            }
            else
            {
                g.setColour (c::textSecondary.withAlpha (0.55f));
                g.fillRect (zone.withSizeKeepingCentre (7.0f, 7.0f));
            }
        }
        else
        {
            const auto& slot = grid.slots[(std::size_t) clip->gridSlot];
            // (a clip looks as it does in the Playlist: the pattern's colour tells the clips apart)
            auto fill = slot.colour != 0 ? juce::Colour (slot.colour)
                        : clip->kind == ClipKind::audio ? c::audioClip
                                                        : theme::tame (PlaylistComponent::patternColour (song, clip->pattern));
            g.setColour (playing ? fill.brighter (0.25f) : fill.withMultipliedBrightness (0.85f));
            g.fillRect (cell);
            if (playing)
            {
                const auto progress = juce::jlimit (0.0f, 1.0f, state->tracks[(std::size_t) t].progress.load (std::memory_order_relaxed));
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.fillRect (cell.withHeight (3).withY (cell.getBottom() - 3).toFloat().withWidth ((float) cell.getWidth() * progress));
            }
            // the launch triangle: lit while playing, blinking while it waits for its grid point
            const auto blinkOn = (juce::Time::getMillisecondCounter() / classicBlinkPhaseMs) % 2 == 0;
            const auto blink = pending && blinkOn;
            // (a clip asked to stop blinks until its grid point: with a long quantisation it would seem to ignore the press)
            const auto stopping = playing && state->tracks[(std::size_t) t].stopping.load (std::memory_order_relaxed);
            const auto a = zone.withSizeKeepingCentre (9.0f, 11.0f);
            // (a light pattern colour takes dark marks)
            const auto ink = (playing ? fill.brighter (0.25f) : fill.withMultipliedBrightness (0.85f)).getPerceivedBrightness() > 0.62f ? c::bg1 : juce::Colours::white;
            theme::fillLaunchTriangle (g, a, stopping && ! blinkOn ? ink.withAlpha (0.3f) : playing ? c::ledOn : pending ? (blink ? c::ledOn : ink.withAlpha (0.3f)) : ink.withAlpha (0.85f));
            if (playing && ink != juce::Colours::white)
            {
                juce::Path triangle;
                triangle.addTriangle (a.getX(), a.getY(), a.getRight(), a.getCentreY(), a.getX(), a.getBottom());
                g.setColour (ink);
                g.strokePath (triangle, juce::PathStrokeType (1.0f));
            }
            g.setColour (ink);
            g.setFont (theme::font (theme::type::small));
            textcache::drawText (g, clipName (song, grid, *clip), cell.withTrimmedLeft (launchZone).withTrimmedRight (3), juce::Justification::centredLeft, true);
            if (! slot.active) // (round 190im: a deactivated clip, greyed)
            {
                g.setColour (c::bg1.withAlpha (0.6f));
                g.fillRect (cell);
            }
        }
        if ((t == selectedTrack && row == selectedScene) || inRange (t, row)) // (round 190lr: a rectangle selection too)
        {
            g.setColour (c::accent);
            g.drawRect (cell, 1);
        }
    }

    // The status cell under a track's slots (round 190bv): its stop button and its arm button.
    void paintStatus (juce::Graphics& g, const Song& song, const launcher::Runtime* state, int t, juce::Rectangle<int> r) const
    {
        if (liveLook())
        {
            paintStatusLive (g, song, state, t, r);
            return;
        }
        namespace c = theme::colour;
        g.setColour (c::bg3);
        g.fillRect (r);
        g.setColour (c::border);
        g.fillRect (r.getRight() - 1, r.getY(), 1, r.getHeight());
        g.setColour (c::textPrimary.withAlpha (0.8f));
        g.fillRect (r.withWidth (launchZone).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
        const auto armed = engine.isGridTrackArmed (t);
        const auto arm = juce::Rectangle<int> (r.getRight() - armWidth - 4, r.getY(), armWidth, r.getHeight()).withSizeKeepingCentre (armWidth, 15).toFloat();
        // while the track records into a slot: a green counter of the length recorded so far, bars.beats.sixteenths
        // from 0.0.0 (round 190cy, Live 12.4.16 live check 190cw: "0.1.0" one beat in, "2.0.2")
        if (const auto* view = state != nullptr ? &state->tracks[(std::size_t) t] : nullptr; view != nullptr && view->recording.load (std::memory_order_relaxed) >= 0)
        {
            const auto elapsed = std::max (0.0, state->clockNow.load (std::memory_order_relaxed) - view->recordFrom.load (std::memory_order_relaxed));
            const auto box = juce::Rectangle<float> ((float) r.getX() + (float) launchZone, (float) r.getY(), arm.getX() - 4.0f - ((float) r.getX() + (float) launchZone), (float) r.getHeight())
                                 .withSizeKeepingCentre (arm.getX() - 4.0f - ((float) r.getX() + (float) launchZone), 15.0f);
            theme::fillRoundRect (g, box, 2.0f, c::ledOn);
            g.setColour (c::bg0);
            g.setFont (theme::font (theme::type::small));
            textcache::drawText (g, juce::String (session::lengthCounter (elapsed, song.meter())), box, juce::Justification::centred, false);
        }
        if (mixer->armsShown()) // (round 190dr: the arm button is in the mixer's Volume section, as in Live)
            return;
        theme::fillRoundRect (g, arm, 2.0f, armed ? c::record : c::bg4);
        if (! armed)
        {
            g.setColour (c::borderStrong);
            g.drawRoundedRectangle (arm.reduced (0.5f), 2.0f, 1.0f);
        }
        g.setColour (armed ? c::bg0 : c::textSecondary);
        g.fillEllipse (arm.withSizeKeepingCentre (7.0f, 7.0f));
    }

    // ---- Live's look of the Session grid (round 190ig; round 190ia CL-3..CL-15, measured in live-session.png,
    // live-launch-states.png, live-clipview-audio.png and live190cw session190bv-02a). Clip-launch layout only.

    // The colour a Live clip is filled with: its own colour, else its track's (Live 12.4 Settings › Theme "Clip Color =
    // Track Color"), else LYNX's colour for its pattern / audio (a project made in LYNX has uncoloured tracks).
    static juce::Colour liveClipFill (const Song& song, const ClipGrid& grid, const Clip& clip, int t)
    {
        const auto& slot = grid.slots[(std::size_t) clip.gridSlot];
        if (slot.colour != 0)
            return juce::Colour (slot.colour);
        if (t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].colour != 0)
            return juce::Colour (song.playlistTracks[(std::size_t) t].colour);
        return clip.kind == ClipKind::audio ? theme::colour::audioClip : theme::tame (PlaylistComponent::patternColour (song, clip.pattern));
    }
    // Live's ink on a clip colour is that colour × 0.24 (7–10:1 on Live's pastels); on a dark LYNX colour that would not
    // read, so a dark fill takes Live's light text instead
    static juce::Colour liveInk (juce::Colour fill)
    {
        // (the Live-pastel style of the shared clip colours, theme::clipBody: ink = colour × 0.24)
        // (round 190oj, CS-6: chosen by contrast, not by brightness: "Mello" #258ed0 took light ink at 2.1:1)
        return theme::liveMarkOn (fill, 0.24f, 4.5f, theme::livelook::text.brighter (0.4f));
    }
    // Live's grid text: LYNX's UI font at 12 px, 12 % wider — Live's own face is wider at the same 7 px cap ("Drums" 30 px
    // in Live, 26 px in LYNX's plain font; round 190ia's font note); Live's face itself is not copied
    static juce::Font liveFont() { return theme::font (theme::type::small).withHorizontalScale (1.12f); }
    // the launch zone of a slot whose left edge is x: Live's 16 px zone, the 1 px divider after it
    static juce::Rectangle<float> liveTriangle (juce::Rectangle<int> cell)
    {
        return { (float) cell.getX() + 4.0f, (float) cell.getCentreY() - 4.5f, 7.5f, 9.0f };
    }

    void paintHeaderLive (juce::Graphics& g, const Song& song, const launcher::Runtime* state, int t, juce::Rectangle<int> r) const
    {
        namespace lv = theme::livelook;
        const auto* info = t < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) t] : nullptr;
        const auto taken = state != nullptr && state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
        // CL-4: the title box filled with the track colour, its name in colour × 0.24 at 4 px, a 2 px separator under it
        const auto box = r.withTrimmedRight (2).withTrimmedBottom (2);
        const auto fill = info != nullptr && info->colour != 0 ? juce::Colour (info->colour) : lv::raised;
        const auto ink = info != nullptr && info->colour != 0 ? liveInk (fill) : lv::text;
        g.setColour (fill);
        g.fillRect (box);
        g.setColour (ink);
        g.setFont (liveFont());
        const auto name = info != nullptr && ! info->name.empty() ? utf8 (info->name) : tr ("Track {0}", { juce::String (t + 1) });
        const auto frozen = frozenChannelOf (song, t) >= 0;
        if (frozen)
            theme::drawFrozenTag (g, juce::Rectangle<float> ((float) box.getRight() - (taken ? 38.0f : 18.0f), (float) box.getCentreY() - 6.5f, 14.0f, 13.0f),
                                  ink, fill);
        g.setColour (ink);
        textcache::drawText (g, name, box.withTrimmedLeft (4).withTrimmedRight ((taken ? 18 : 0) + (frozen ? 18 : 0)), juce::Justification::centredLeft, false);
        if (taken)
        {
            const auto mark = box.withTrimmedLeft (box.getWidth() - 20).withSizeKeepingCentre (14, 11).toFloat();
            g.drawRect (mark, 1.0f);
            g.drawLine (mark.getX(), mark.getBottom(), mark.getRight(), mark.getY(), 1.4f);
        }
    }

    // Round 190kc: Live's return tracks (live-session.png x 1390..1539: "A Reverb", "B Delay"): a title box in the
    // return's colour with its letter and name, then the column without slots — the rows' 1 px separators on #363636 —
    // down to the status row. The classic look draws the same on its own greys.
    void paintReturnColumns (juce::Graphics& g, juce::Rectangle<int> cells) const
    {
        const auto count = returnColumnsNow();
        if (count == 0)
            return;
        const auto& song = engine.getSong();
        const auto returns = sessionmixer::returnsOf (song);
        const auto live = liveLook();
        namespace c = theme::colour;
        namespace lv = theme::livelook;
        for (int k = 0; k < count && k < (int) returns.size(); ++k)
        {
            const juce::Rectangle<int> column (cells.getRight() + k * columnWidth, 0, columnWidth, cells.getBottom() + statusHeight);
            if (ui::paintCache() && ! g.clipRegionIntersects (column))
                continue;
            const auto& m = song.mixer[(std::size_t) returns[(std::size_t) k]];
            const auto title = utf8 (sessionmixer::returnTitle (song, returns[(std::size_t) k]));
            if (live)
            {
                g.setColour (lv::separator);
                g.fillRect (column);
                const auto box = column.withHeight (headerHeight).withTrimmedRight (2).withTrimmedBottom (2);
                const auto fill = m.colour != 0 ? juce::Colour (m.colour) : lv::raised;
                g.setColour (fill);
                g.fillRect (box);
                // (Live: the name in colour x 0.24 on every return colour, also #7B7B7B's "B Delay", measured)
                g.setColour (m.colour != 0 ? theme::liveShade (fill, 0.24f) : lv::text);
                g.setFont (liveFont());
                textcache::drawText (g, title, box.withTrimmedLeft (4), juce::Justification::centredLeft, true);
                g.setColour (lv::surface);
                const auto body = juce::Rectangle<int> (column.getX(), headerHeight, columnWidth - 2, cells.getHeight() + statusHeight);
                for (int r = firstRow(); r < rowCount() && rowY (r) < cells.getBottom(); ++r)
                    g.fillRect (juce::Rectangle<int> (column.getX(), rowY (r), columnWidth - 2, (int) rowHeight - 1).getIntersection (body.withBottom (cells.getBottom())));
                if (rowY (rowCount()) < cells.getBottom())
                    g.fillRect (juce::Rectangle<int> (column.getX(), rowY (rowCount()), columnWidth - 2, cells.getBottom() - rowY (rowCount())).getIntersection (body));
                g.fillRect (column.getX(), cells.getBottom() + 1, columnWidth - 2, statusHeight - 1);
            }
            else
            {
                g.setColour (c::bg2);
                g.fillRect (column);
                const auto head = column.withHeight (headerHeight);
                g.setColour (c::bg3);
                g.fillRect (head);
                if (m.colour != 0)
                {
                    g.setColour (juce::Colour (m.colour));
                    g.fillRect (head.getX(), head.getBottom() - 3, head.getWidth(), 3);
                }
                g.setColour (c::textPrimary);
                g.setFont (theme::font (theme::type::small, true));
                textcache::drawText (g, title, head.withTrimmedLeft (6).withTrimmedRight (4), juce::Justification::centredLeft, true);
                g.setColour (c::border);
                g.fillRect (column.getRight() - 1, 0, 1, column.getHeight());
            }
        }
    }

    // Live's pulse of a waiting launch or stop (CL-11, CL-12), in a colour
    static juce::Colour livePulsed (juce::Colour c) { return theme::liveShade (c, livePulse()); }

    void paintSlotLive (juce::Graphics& g, const Song& song, const ClipGrid& grid, const launcher::Runtime* state, int t, int row, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        namespace lv = theme::livelook;
        const auto* clip = launcher::clipAt (grid, t, row);
        const auto cell = r.withTrimmedRight (2).withTrimmedBottom (1);
        const auto zone = cell.withWidth (16);
        const auto* view = state != nullptr ? &state->tracks[(std::size_t) t] : nullptr;
        const auto playing = view != nullptr && view->playing.load (std::memory_order_relaxed) == row;
        const auto pending = view != nullptr && view->pending.load (std::memory_order_relaxed) == row;
        const auto recording = view != nullptr && view->recording.load (std::memory_order_relaxed) == row;
        const auto waiting = view != nullptr && view->recordPending.load (std::memory_order_relaxed) == row;
        const auto stopping = playing && view->stopping.load (std::memory_order_relaxed);
        const auto selected = inRange (t, row); // (round 190lr, DL-29: every slot of a rectangle selection)
        const auto focused = hasKeyboardFocus (true);
        const auto selection = focused ? lv::selectionFocused : lv::selectionUnfocused;
        const auto hoveredButton = hovering (HoverButton::slot, t, row); // (round 190lr, DL-20: the glyph grows by 2 px)
        const auto triangle = hoveredButton ? liveTriangle (cell).expanded (1.0f) : liveTriangle (cell);

        if (clip == nullptr && ! recording)
        {
            // an empty slot: #363636, #464646 in the selected track's column and the selected scene's row (CL-8), the
            // selection's own colour when selected (CL-7); its stop square, or an armed track's record circle (CL-9)
            g.setColour (selected ? selection : t == selectedTrack || row == selectedScene ? lv::raised : lv::surface);
            g.fillRect (cell);
            const auto mark = juce::Rectangle<float> ((float) cell.getX() + 4.0f, (float) cell.getCentreY() - 4.5f, 9.0f, 9.0f).expanded (hoveredButton ? 1.0f : 0.0f);
            if (waiting) // (a slot waiting for its recording: the circle pulses in the record red — Live not captured)
            {
                g.setColour (livePulsed (lv::arm));
                g.fillEllipse (mark);
            }
            else if (engine.isGridTrackArmed (t))
            {
                g.setColour (lv::well);
                g.fillEllipse (mark);
            }
            else
            {
                // a stop asked for on an empty slot of a playing track blinks its square in the queued greens (CL-11)
                g.setColour (pending ? livePulsed (lv::playing) : lv::well);
                g.fillRect (mark);
            }
            return;
        }

        // a clip (or the clip a recording makes): the clip colour, Live's dark ink, the divider after the launch zone
        const auto trackColour = t < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) t].colour : 0u;
        const auto fill = clip != nullptr ? liveClipFill (song, grid, *clip, t)
                          : trackColour != 0 ? juce::Colour (trackColour)
                                             : theme::tame (PlaylistComponent::patternColour (song, (int) song.patterns.size()));
        // round 190im (R12, DL-15): a deactivated clip is drawn greyed (Live's look not captured: LYNX's grey of the clip's
        // own brightness, its name dimmed)
        const auto inactive = clip != nullptr && clip->gridSlot >= 0 && clip->gridSlot < (int) grid.slots.size() && ! grid.slots[(std::size_t) clip->gridSlot].active;
        const auto shown = inactive ? juce::Colour::greyLevel (0.22f + 0.18f * fill.getPerceivedBrightness()) : fill;
        const auto ink = inactive ? lv::text.withAlpha (0.55f) : liveInk (fill);
        g.setColour (shown);
        g.fillRect (cell);
        // two clips one above the other: Live's dark tinted 1 px between them (instead of the #242424 separator)
        if (row + 1 < rowCount() && launcher::clipAt (grid, t, row + 1) != nullptr)
        {
            g.setColour (theme::liveShade (shown, 0.06f));
            g.fillRect (cell.getX(), cell.getBottom(), cell.getWidth(), 1);
        }
        g.setColour (theme::liveShade (shown, 0.6f));
        g.fillRect (cell.getX() + 16, cell.getY(), 1, cell.getHeight());
        // the launch zone: a #181818 box while the clip plays, waits for its start or its stop, or records (CL-10..13)
        const auto active = playing || pending || recording;
        if (active)
        {
            g.setColour (lv::stateBox);
            g.fillRect (zone);
        }
        else if (selected)
        {
            g.setColour (focused ? lv::selectionFocusedFill : selection);
            g.fillRect (zone);
        }
        if (selected)
        {
            g.setColour (selection);
            g.drawRect (zone.withWidth (17), 1);
        }
        // (a stop asked for: Live keeps the triangle green and pulses the track's stop square, paintStatusLive)
        juce::ignoreUnused (stopping);
        // round 190im (DL-13, Live's manual 16.7: "Clips and scenes with assigned Follow Actions are indicated by a
        // striped Clip/Scene Launch button"): diagonal stripes across the launch zone, in the clip colour's shade
        if (clip != nullptr && clip->gridSlot >= 0 && clip->gridSlot < (int) grid.slots.size())
            if (const auto& follow = grid.slots[(std::size_t) clip->gridSlot].follow; follow.enabled && (follow.a != FollowKind::none || follow.b != FollowKind::none))
            {
                const juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (zone);
                g.setColour (active ? juce::Colour (0xff2c2c2c) : theme::liveShade (shown, 0.5f));
                for (int k = -zone.getHeight(); k < zone.getWidth(); k += 4)
                    g.drawLine ((float) (zone.getX() + k), (float) zone.getBottom(), (float) (zone.getX() + k + zone.getHeight()), (float) zone.getY(), 1.0f);
            }
        const auto triangleColour = recording ? lv::arm
                                  : playing ? lv::playing
                                  : pending ? livePulsed (lv::playing)
                                  : selected ? lv::well
                                  : theme::liveShade (shown, 0.4f);
        theme::fillLaunchTriangle (g, triangle, triangleColour);
        g.setColour (ink);
        g.setFont (liveFont());
        const auto name = clip != nullptr ? clipName (song, grid, *clip)
                          : engine.gridTrackInput (t) == AudioEngine::GridInput::midi ? utf8 (engine.slotRecordingName (t)) : juce::String();
        textcache::drawText (g, name, cell.withTrimmedLeft (20).withTrimmedRight (2), juce::Justification::centredLeft, false);
    }

    // The track status cell (CL-15): a 17 px #363636 cell under a 2 px separator with the 9 x 9 stop square; while a clip
    // plays "loops ◔ length": the loop count, an 11 px pie in the track colour filling with the clip's position, the loop
    // length in beats ("+" when it is not whole; filmed in Live 12.4: the 32-beat Drums loop shows "1 ◔ 32", then "5 ◔ 32"); while recording the length counter on the track colour (CL-13)
    void paintStatusLive (juce::Graphics& g, const Song& song, const launcher::Runtime* state, int t, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        namespace lv = theme::livelook;
        const auto cell = r.withTrimmedTop (2).withTrimmedRight (2);
        g.setColour (t == selectedTrack || trackSet.count (t) > 0 ? lv::raised : lv::surface);
        g.fillRect (cell);
        const auto* view = state != nullptr ? &state->tracks[(std::size_t) t] : nullptr;
        // (a stop waiting for its grid point: the square pulses black ↔ the playing green, as in Live — film3)
        g.setColour (view != nullptr && view->stopping.load (std::memory_order_relaxed) ? livePulsed (lv::playing) : lv::well);
        g.fillRect (juce::Rectangle<float> ((float) cell.getX() + 4.0f, (float) cell.getCentreY() - 4.5f, 9.0f, 9.0f)
                        .expanded (hovering (HoverButton::trackStop, t, -1) ? 1.0f : 0.0f)); // (round 190lr, DL-20)
        const auto armShown = ! mixer->armsShown();
        const auto arm = juce::Rectangle<int> (cell.getRight() - armWidth - 2, cell.getY(), armWidth, cell.getHeight()).withSizeKeepingCentre (armWidth, 13).toFloat();
        const auto right = armShown ? (int) arm.getX() - 2 : cell.getRight() - 1;
        const auto trackColour = t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].colour != 0
                                     ? juce::Colour (song.playlistTracks[(std::size_t) t].colour) : lv::playing;
        if (view != nullptr && view->recording.load (std::memory_order_relaxed) >= 0)
        {
            const auto elapsed = std::max (0.0, state->clockNow.load (std::memory_order_relaxed) - view->recordFrom.load (std::memory_order_relaxed));
            const auto box = juce::Rectangle<float> ((float) cell.getX() + 16.0f, (float) cell.getCentreY() - 6.5f, (float) (right - cell.getX() - 16), 13.0f);
            g.setColour (juce::Colour (0xff111111));
            g.fillRect (box);
            g.setColour (trackColour);
            g.fillRect (box.reduced (1.0f));
            g.setColour (liveInk (trackColour));
            g.setFont (liveFont());
            textcache::drawText (g, juce::String (session::lengthCounter (elapsed, song.meter())), box, juce::Justification::centred, false);
        }
        else if (const auto playing = view != nullptr ? view->playing.load (std::memory_order_relaxed) : -1; playing >= 0)
        {
            const auto* grid = gridOf();
            const auto* clip = grid != nullptr ? launcher::clipAt (*grid, t, playing) : nullptr;
            const auto progress = juce::jlimit (0.0f, 1.0f, view->progress.load (std::memory_order_relaxed));
            const auto colour = clip != nullptr ? liveClipFill (song, *grid, *clip, t) : trackColour;
            const auto area = juce::Rectangle<int> (cell.getX() + 14, cell.getY(), right - cell.getX() - 14, cell.getHeight());
            const auto full = area.getWidth() >= 52;
            const auto pie = juce::Rectangle<float> (full ? (float) area.getX() + 21.0f : (float) area.getCentreX() - 6.0f, (float) cell.getCentreY() - 6.0f, 12.0f, 12.0f);
            // round 190im (DL-9, Live's manual 7.3): a one-shot clip shows a progress bar and its time left as m:ss
            // instead of the pie, the loop count and the length
            if (const auto left = oneShotSecondsLeft (t, playing, progress); left >= 0.0)
            {
                const auto bar = juce::Rectangle<float> (pie.getX() - 4.0f, pie.getCentreY() - 3.5f, 16.0f, 7.0f);
                g.setColour (juce::Colour (0xff262626));
                g.fillRect (bar);
                g.setColour (theme::liveShade (colour, 0.74f));
                g.fillRect (bar.reduced (1.0f));
                g.setColour (theme::liveShade (colour, 0.86f));
                g.fillRect (bar.reduced (1.0f).withWidth ((bar.getWidth() - 2.0f) * progress));
                if (full)
                {
                    const auto secs = (int) std::ceil (left);
                    g.setColour (lv::text);
                    g.setFont (liveFont());
                    textcache::drawText (g, juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2),
                                juce::Rectangle<float> (bar.getRight() + 5.0f, (float) cell.getY(), (float) right - bar.getRight() - 5.0f, (float) cell.getHeight()),
                                juce::Justification::centredLeft, false);
                }
            }
            else
            {
            g.setColour (juce::Colour (0xff262626));
            g.fillEllipse (pie);
            g.setColour (theme::liveShade (colour, 0.74f));
            g.fillEllipse (pie.reduced (1.0f));
            if (progress > 0.0f)
            {
                juce::Path wedge;
                wedge.addPieSegment (pie.reduced (1.0f), 0.0f, juce::MathConstants<float>::twoPi * progress, 0.0f);
                g.setColour (theme::liveShade (colour, 0.86f));
                g.fillPath (wedge);
            }
            if (full && clip != nullptr)
            {
                const auto length = (double) std::max<std::int64_t> (1, clip->length);
                const auto loops = (int) std::floor (std::max (0.0, state->clockNow.load (std::memory_order_relaxed) - view->anchor.load (std::memory_order_relaxed)) / length) + 1;
                const auto beats = length / (double) lynx::ppq; // (Live: "1 ◔ 32" for the 32-beat Drums loop of the Patience set)
                const auto whole = (int) std::floor (beats + 1.0e-9);
                g.setColour (lv::text);
                g.setFont (liveFont());
                textcache::drawText (g, juce::String (loops), juce::Rectangle<float> ((float) area.getX(), (float) cell.getY(), pie.getX() - 4.0f - (float) area.getX(), (float) cell.getHeight()),
                            juce::Justification::centredRight, false);
                // (Live 12.4, details2_190ld shots/ref/live-status-session-playing-a.png: an uneven length, "31+", in #e75f3a)
                if (beats - whole > 1.0e-6)
                    g.setColour (juce::Colour (0xffe75f3a));
                textcache::drawText (g, juce::String (whole) + (beats - whole > 1.0e-6 ? "+" : ""),
                            juce::Rectangle<float> (pie.getRight() + 5.0f, (float) cell.getY(), (float) right - pie.getRight() - 5.0f, (float) cell.getHeight()),
                            juce::Justification::centredLeft, false);
            }
            }
        }
        else if (const auto icon = monitorIcon (t, engine.isGridTrackArmed (t)); icon != 0)
            // Live 12.4 (details2_190ld raw/L08): the monitor icon replaces the miniature, centred in its 55 px
            paintMonitorIcon (g, icon, juce::Rectangle<int> (cell.getX() + 17, cell.getY() + 2, right - 1 - cell.getX() - 17, cell.getHeight() - 4));
        else if (view == nullptr || ! view->taken.load (std::memory_order_relaxed))
            paintMiniature (g, song, t, juce::Rectangle<int> (cell.getX() + 17, cell.getY() + 2, right - 1 - cell.getX() - 17, cell.getHeight() - 4));
        if (! armShown)
            return;
        const auto armed = engine.isGridTrackArmed (t);
        g.setColour (armed ? lv::arm : lv::well);
        g.fillRect (arm);
        g.setColour (armed ? lv::ink : lv::text);
        g.fillEllipse (arm.withSizeKeepingCentre (7.0f, 7.0f));
    }

    // Round 190ld (DL-11): Live's monitor icons, measured in Live 12.4 (details2_190ld raw/L08), drawn as LYNX vectors:
    // a MIDI track's 22 x 11 keyboard (a #757575 frame and key lines every 3 px, the black keys 3 x 6 with a #b5b5b5 core) and an audio track's
    // 7 x 11 microphone (#b5b5b5: a 3 x 7 head in a U-shaped holder on a stem)
    static void paintMonitorIcon (juce::Graphics& g, int icon, juce::Rectangle<int> area)
    {
        namespace lv = theme::livelook;
        const juce::Colour frame { 0xff757575 };
        if (icon == 2)
        {
            const auto k = juce::Rectangle<int> (22, 11).withCentre (area.getCentre());
            g.setColour (frame);
            g.drawRect (k, 1);
            for (int i = 1; i < 7; ++i) // the white keys' edges, every 3 px
                g.fillRect (k.getX() + i * 3, k.getY() + 1, 1, k.getHeight() - 2);
            for (const auto i : { 1, 2, 4, 5, 6 }) // C# D#, F# G# A#: 3 x 6 in the frame grey with a #b5b5b5 core (Live's)
            {
                g.setColour (frame);
                g.fillRect (k.getX() + i * 3 - 1, k.getY() + 1, 3, 6);
                g.setColour (lv::text);
                g.fillRect (k.getX() + i * 3, k.getY() + 1, 1, 6);
            }
            return;
        }
        const auto m = juce::Rectangle<int> (7, 11).withCentre (area.getCentre());
        g.setColour (lv::text);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) m.getX() + 2.0f, (float) m.getY(), 3.0f, 7.0f), 1.2f);
        g.fillRect (m.getX(), m.getY() + 4, 1, 3);
        g.fillRect (m.getRight() - 1, m.getY() + 4, 1, 3);
        g.fillRect (m.getX() + 1, m.getY() + 7, 1, 1);
        g.fillRect (m.getRight() - 2, m.getY() + 7, 1, 1);
        g.fillRect (m.getX() + 2, m.getY() + 8, 3, 1);
        g.fillRect (m.getX() + 3, m.getY() + 9, 1, 2);
    }

    // Round 190ld (DL-10): the Arrangement miniature of track `t` in `area` (Live: 55 x 13 px, blocks and line alike)
    void paintMiniature (juce::Graphics& g, const Song& song, int t, juce::Rectangle<int> area) const
    {
        namespace lv = theme::livelook;
        if (area.getWidth() < 4 || area.getHeight() < 4)
            return;
        const auto span = (double) miniatureBeats * (double) lynx::ppq;
        const auto pxPerTick = (double) area.getWidth() / span;
        const auto step = (double) miniatureStepTicks (song);
        const auto pos = std::floor (juce::jmax (0.0, engine.getTransport().positionTicks.load (std::memory_order_relaxed)) / step) * step;
        const auto half = std::floor ((double) area.getWidth() * 0.5) / pxPerTick; // (the line stops at the middle)
        const auto from = juce::jmax (0.0, pos - half);
        const auto trackColour = t < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) t].colour : 0u;
        const auto blocks = area.toFloat();
        for (const auto& clip : song.playlist)
        {
            if (clip.track != t || clip.length <= 0 || clip.muted || (double) (clip.start + clip.length) <= from || (double) clip.start >= from + span)
                continue;
            const auto x0 = std::round (blocks.getX() + (float) (juce::jmax (0.0, (double) clip.start - from) * pxPerTick));
            const auto x1 = std::round (blocks.getX() + (float) (juce::jmin (span, (double) (clip.start + clip.length) - from) * pxPerTick));
            const auto colour = trackColour != 0 ? juce::Colour (trackColour)
                              : clip.kind == ClipKind::audio ? theme::colour::audioClip : theme::tame (PlaylistComponent::patternColour (song, clip.pattern));
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (x0, blocks.getY(), juce::jmax (1.0f, x1 - x0), blocks.getHeight()));
        }
        g.setColour (lv::text);
        g.fillRect (area.getX() + juce::roundToInt ((pos - from) * pxPerTick), area.getY(), 1, area.getHeight());
    }

    void paintScene (juce::Graphics& g, const ClipGrid& grid, int row, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        static const ClipGridScene unnamed;
        const auto& scene = row < (int) grid.scenes.size() ? grid.scenes[(std::size_t) row] : unnamed;
        if (liveLook())
        {
            paintSceneLive (g, scene, row, r);
            return;
        }
        const auto cell = r.reduced (1);
        g.setColour (scene.colour != 0 ? juce::Colour (scene.colour) : c::bg3);
        g.fillRect (cell);
        const auto a = cell.withWidth (launchZone).toFloat().withSizeKeepingCentre (9.0f, 11.0f);
        if (scene.follow.enabled && (scene.follow.a != FollowKind::none || scene.follow.b != FollowKind::none))
            paintFollowStripes (g, cell.withWidth (launchZone), (scene.colour != 0 ? juce::Colour (scene.colour) : c::bg3).contrasting (0.18f));
        // (round 190em: a scene with its own tempo or time signature has a coloured launch triangle, as Live's cyan one)
        // (a plain scene's triangle: Live's dark grey, measured in the 190ei shot live-scenes-tempo-sig-on.png)
        const auto queuedOn = row == queuedScene && (! ui::motion::enabled() || (juce::Time::getMillisecondCounter() / classicBlinkPhaseMs) % 2 == 0);
        theme::fillLaunchTriangle (g, a, queuedOn ? c::ledOn
                                         : scene.tempo > 0.0 || scene.beatsPerBar > 0 ? c::info
                                         : layoutstyle::current() == layoutstyle::Style::clipLaunch ? liveSceneTriangle : c::textPrimary);
        g.setFont (theme::font (theme::type::small));
        auto nameArea = cell.withTrimmedLeft (launchZone);
        // round 190ef: the scene's tempo and time signature fields (greyed: off, showing the song's value)
        if (sceneFieldsOn)
        {
            nameArea.removeFromRight (sceneFieldsWidth);
            const auto& song = engine.getSong();
            const auto field = [&] (juce::Rectangle<int> box, const juce::String& text, bool on) {
                theme::fillRoundRect (g, box.toFloat().reduced (0.5f, 3.0f), 2.0f, on ? c::bg1 : c::bg2);
                g.setColour (on ? c::textPrimary : c::textDisabled);
                textcache::drawText (g, text, box, juce::Justification::centred, false);
            };
            // (off: the default drawn dim, as the reference does; round 190em: the signature as "n / d")
            field (sceneFieldBox (r, 1), utf8 (session::sceneTempoText (scene.tempo > 0.0 ? scene.tempo : song.bpm)), scene.tempo > 0.0);
            field (sceneFieldBox (r, 2), utf8 (scene.beatsPerBar > 0 ? session::sceneSignatureText (scene.beatsPerBar, scene.beatUnit)
                                                                     : session::sceneSignatureText (song.beatsPerBar, 4)), scene.beatsPerBar > 0);
            if (sceneSelected && row == selectedScene && selectedField != 0) // (the selected field)
            {
                g.setColour (c::accent.withAlpha (0.8f));
                g.drawRect (sceneFieldBox (r, selectedField).reduced (0, 3), 1);
            }
        }
        // (round 190ig, CL-14a: the name's own colour; it took the launch triangle's, Live's #1E1E1E on #23262C = 1.1:1)
        g.setColour (c::textPrimary);
        textcache::drawText (g, ! scene.name.empty() ? utf8 (scene.name) : juce::String (row + 1), nameArea, juce::Justification::centredLeft, true);
        if (sceneSelected && row == selectedScene) // (round 190ef: a selected scene, as a selected slot)
        {
            g.setColour (c::accent);
            g.drawRect (cell, 2);
        }
    }

    // A scene cell in Live's look (round 190ia CL-14a/b, live-session.png 1540,86,1630,340 and live-launch-states.png):
    // a 17 px #363636 cell over a 1 px #242424 separator, a 7 x 9 #1E1E1E launch triangle 4 px in, the name centred
    // between it and the scene number at the right (both #B5B5B5, cut without an ellipsis as Live cuts "Breakdo"); a
    // selected scene #B0DDEB with #070707 text. A scene with its own tempo or time signature keeps LYNX's cyan triangle.
    void paintSceneLive (juce::Graphics& g, const ClipGridScene& scene, int row, juce::Rectangle<int> r) const
    {
        namespace c = theme::colour;
        namespace lv = theme::livelook;
        const auto cell = r.withTrimmedLeft (2).withTrimmedBottom (1);
        const auto selected = sceneSelected && row == selectedScene;
        const auto fill = selected ? lv::selectionFocused : scene.colour != 0 ? juce::Colour (scene.colour) : lv::surface;
        g.setColour (fill);
        g.fillRect (cell);
        const auto ink = selected ? lv::ink : scene.colour != 0 ? theme::liveShade (juce::Colour (scene.colour), 0.24f) : lv::text;
        const auto a = juce::Rectangle<float> ((float) cell.getX() + 4.0f, (float) cell.getCentreY() - 4.5f, 7.0f, 9.0f)
                           .expanded (hovering (HoverButton::scene, -1, row) ? 1.0f : 0.0f); // (round 190lr, DL-20)
        // round 190lr (DL-14, Live's manual 16.7): a scene with a follow action has a striped Scene Launch button, as a clip
        if (scene.follow.enabled && (scene.follow.a != FollowKind::none || scene.follow.b != FollowKind::none))
            paintFollowStripes (g, cell.withWidth (14), theme::liveShade (fill, 0.5f));
        // (round 190im, DL-5: a launched scene's button pulses until its clips start, as a queued clip's does)
        theme::fillLaunchTriangle (g, a, row == queuedScene ? livePulsed (lv::playing) : scene.tempo > 0.0 || scene.beatsPerBar > 0 ? c::info : lv::well);
        g.setFont (liveFont()); // (round 190im: the grid's wider Live text, as the slots and headers: round 190ig's follow-up)
        auto nameArea = cell.withTrimmedLeft (14);
        if (sceneFieldsOn)
        {
            // the fields keep round 190ef's places at the right (Live's look with them on was not captured: no number)
            nameArea.removeFromRight (sceneFieldsWidth);
            const auto& song = engine.getSong();
            const auto field = [&] (juce::Rectangle<int> box, const juce::String& text, bool on) {
                theme::fillRoundRect (g, box.toFloat().reduced (0.5f, 3.0f), 2.0f, on ? lv::well : lv::raised);
                g.setColour (on ? lv::text : lv::text.withAlpha (0.55f));
                textcache::drawText (g, text, box, juce::Justification::centred, false);
            };
            field (sceneFieldBox (r, 1), utf8 (session::sceneTempoText (scene.tempo > 0.0 ? scene.tempo : song.bpm)), scene.tempo > 0.0);
            field (sceneFieldBox (r, 2), utf8 (scene.beatsPerBar > 0 ? session::sceneSignatureText (scene.beatsPerBar, scene.beatUnit)
                                                                     : session::sceneSignatureText (song.beatsPerBar, 4)), scene.beatsPerBar > 0);
            if (selected && selectedField != 0)
            {
                g.setColour (lv::ink);
                g.drawRect (sceneFieldBox (r, selectedField).reduced (0, 3), 1);
            }
        }
        else
        {
            g.setColour (ink);
            textcache::drawText (g, juce::String (row + 1), nameArea.removeFromRight (24).withTrimmedRight (4), juce::Justification::centred, false);
        }
        g.setColour (ink);
        textcache::drawText (g, utf8 (scene.name), nameArea, juce::Justification::centred, false);
    }

    // diagonal stripes across a launch button (Live: "Clips and scenes with assigned Follow Actions are indicated by a
    // striped Clip/Scene Launch button"), the clip version's 4 px pitch
    static void paintFollowStripes (juce::Graphics& g, juce::Rectangle<int> zone, juce::Colour colour)
    {
        const juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (zone);
        g.setColour (colour);
        for (int k = -zone.getHeight(); k < zone.getWidth(); k += 4)
            g.drawLine ((float) (zone.getX() + k), (float) zone.getBottom(), (float) (zone.getX() + k + zone.getHeight()), (float) zone.getY(), 1.0f);
    }

    // round 190ef: the scene cell's fields: 1 tempo, 2 / 3 the time signature (in the cell `r`; both parts share one box)
    static juce::Rectangle<int> sceneFieldBox (juce::Rectangle<int> r, int field)
    {
        const auto right = r.getRight() - 3;
        return field >= 2 ? juce::Rectangle<int> (right - signatureFieldWidth, r.getY(), signatureFieldWidth, r.getHeight())
                          : juce::Rectangle<int> (right - signatureFieldWidth - 2 - tempoFieldWidth, r.getY(), tempoFieldWidth, r.getHeight());
    }
    // the field of scene `scene` at p (0: none)
    int sceneFieldAt (juce::Point<int> p, int scene) const
    {
        if (! sceneFieldsOn || scene < 0)
            return 0;
        const juce::Rectangle<int> r (sceneX(), rowY (scene), sceneWidthNow(), rowHeight);
        if (sceneFieldBox (r, 1).contains (p))
            return 1;
        if (const auto box = sceneFieldBox (r, 2); box.contains (p))
            return p.x < box.getCentreX() ? 2 : 3; // (round 190em: the numerator and the denominator, left and right of "/")
        return 0;
    }

    void putPattern (int track, int scene, int pattern)
    {
        engine.editSong ([=] (Song& s) {
            if (pattern < 0 || pattern >= (int) s.patterns.size())
                return;
            Clip clip;
            clip.pattern = pattern;
            clip.length = std::max<std::int64_t> (ppq, s.patterns[(std::size_t) pattern].effectiveLength (s.meter()));
            launcher::setClip (s, track, scene, clip);
        });
        repaint();
    }

    // An audio file (from the Browser or from outside) as a clip in a slot: one pass of the file at the project's tempo.
    bool putAudioFile (const juce::File& file, int track, int scene)
    {
        if (! SampleLibrary::isSampleFile (file))
            return false;
        const auto data = engine.getSampleLibrary().load (file.getFullPathName());
        if (data == nullptr)
            return false;
        const auto seconds = (double) data->frames() / data->sampleRate;
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (seconds * engine.getSong().bpm / 60.0 * ppq));
        const auto path = file.getFullPathName().toStdString();
        const auto name = file.getFileNameWithoutExtension().toStdString();
        engine.editSong ([=] (Song& s) {
            int channel = -1;
            for (int c = 0; c < (int) s.channels.size(); ++c)
                if (s.channels[(std::size_t) c].instrument == InstrumentKind::audioClip && s.channels[(std::size_t) c].samplePath == path)
                    channel = c;
            const auto made = channel < 0;
            if (made)
            {
                ChannelDef c;
                c.name = name;
                c.instrument = InstrumentKind::audioClip;
                c.sampler.declickMode = audioClipDeclickMode;
                c.samplePath = path;
                c.mixerTrack = s.claimFreeInsert();
                s.channels.push_back (c);
                channel = (int) s.channels.size() - 1;
            }
            if (! session::putAudio (s, track, scene, channel, length) && made)
                s.channels.pop_back();
        });
        return true;
    }

    // --- the clipboard (round 190bv): shared with the Playlist, so a clip goes both ways -----------------------------
    // round 190lx (DL-29): the rectangle selection's corners (t0 <= t1, s0 <= s1); false without one around (track, scene)
    bool rangeAround (int track, int scene, int& t0, int& s0, int& t1, int& s1) const noexcept
    {
        if (! rangeLive() || ! inRange (track, scene))
            return false;
        t0 = juce::jmin (rangeTrack0, selectedTrack), t1 = juce::jmax (rangeTrack0, selectedTrack);
        s0 = juce::jmin (rangeScene0, selectedScene), s1 = juce::jmax (rangeScene0, selectedScene);
        return t0 != t1 || s0 != s1;
    }
    void selectRectangle (int t0, int s0, int t1, int s1)
    {
        t1 = juce::jmin (t1, columnCount() - 1);
        sceneSelected = titleSelected = false;
        rangeTrack0 = t0, rangeScene0 = s0, selectedTrack = juce::jmax (t0, t1), selectedScene = s1;
        markRangeEnd();
        revealSelection();
        notify (GridPick::select);
    }
    void copySlot (int track, int scene, bool cutToo)
    {
        if (int t0, s0, t1, s1; rangeAround (track, scene, t0, s0, t1, s1)) // round 190lx (DL-29): the whole rectangle
        {
            auto block = session::copyBlock (engine.getSong(), t0, s0, t1, s1);
            if (block.empty())
                return;
            auto first = block.clips.front().content.clip;
            first.gridSlot = -1;
            first.uid = 0;
            first.start = 0;
            PlaylistComponent::setCopiedClips ({ first }, carriedOf (block.clips.front().content)); // (round 190ne: a track clip travels along)
            PlaylistComponent::publishCopiedClips (engine); // (issue #232: its first clip to another LYNX SHARE window)
            copiedBlock = std::move (block);
            copiedSlot.reset();
            copiedSlotStamp = PlaylistComponent::copiedStamp();
            if (cutToo)
                engine.editSong ([=] (Song& s) { session::clearBlock (s, t0, s0, t1, s1); });
            repaint();
            return;
        }
        auto content = session::copyOf (engine.getSong(), track, scene);
        if (! content)
            return;
        copiedBlock.reset();
        auto clip = content->clip;
        clip.gridSlot = -1;
        clip.uid = 0;
        clip.start = 0;
        PlaylistComponent::setCopiedClips ({ clip }, carriedOf (*content)); // (round 190ne: a track clip travels along)
        PlaylistComponent::publishCopiedClips (engine); // (issue #232: and to another LYNX SHARE window)
        copiedSlot = std::move (content);
        copiedSlotStamp = PlaylistComponent::copiedStamp();
        if (cutToo)
            engine.editSong ([=] (Song& s) { launcher::clearClip (s, track, scene); });
        repaint();
    }
    // round 190ne (core/TrackClips.h): what a copied slot's track clip gives the shared clipboard
    static trackclips::Carried carriedOf (const session::SlotClip& content)
    {
        trackclips::Carried carried;
        if (content.trackClip)
            carried.patterns.emplace_back (content.clip.pattern, *content.trackClip);
        return carried;
    }
    static bool canPaste()
    {
        if ((copiedSlot || copiedBlock) && copiedSlotStamp == PlaylistComponent::copiedStamp())
            return true;
        if (windowclip::hasIncoming (crossclipboard::Kind::clips)) // (issue #232: copied in another LYNX SHARE window)
            return true;
        const auto& clips = PlaylistComponent::copiedClips();
        return std::any_of (clips.begin(), clips.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern || c.kind == ClipKind::audio; });
    }
    void pasteSlot (int track, int scene)
    {
        // issue #232 (round 190nz): a clip copied in another LYNX SHARE window: its first pattern or audio clip, with what
        // it plays (core/CrossPaste.h)
        if (auto incoming = windowclip::incoming (crossclipboard::Kind::clips))
        {
            auto& clips = incoming->fragment.playlist;
            clips.erase (std::remove_if (clips.begin(), clips.end(), [] (const Clip& c) { return c.kind == ClipKind::automation; }), clips.end());
            if (! clips.empty())
            {
                clips.resize (1);
                engine.editSong ([&incoming, track, scene] (Song& s) {
                    const auto pasted = crosspaste::importClips (s, incoming->fragment);
                    if (! pasted.empty())
                        session::putClip (s, pasted.front(), track, scene);
                });
                ui::postHint ({ windowclip::pastedFrom (*incoming) }, 2500);
                repaint();
                return;
            }
        }
        if (copiedBlock && copiedSlotStamp == PlaylistComponent::copiedStamp()) // round 190lx (DL-29): top-left on the slot
        {
            engine.editSong ([=, block = *copiedBlock] (Song& s) { session::pasteBlock (s, track, scene, block); });
            if (copiedBlock->tracks > 1 || copiedBlock->scenes > 1)
                selectRectangle (track, scene, track + copiedBlock->tracks - 1, scene + copiedBlock->scenes - 1);
            repaint();
            return;
        }
        if (copiedSlot && copiedSlotStamp == PlaylistComponent::copiedStamp())
            engine.editSong ([=, content = *copiedSlot] (Song& s) { session::paste (s, track, scene, content); });
        else
        {
            // copied in the Playlist: its first pattern or audio clip
            const auto& clips = PlaylistComponent::copiedClips();
            const auto it = std::find_if (clips.begin(), clips.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern || c.kind == ClipKind::audio; });
            if (it == clips.end())
                return;
            engine.editSong ([=, clip = *it, carried = PlaylistComponent::copiedPatterns()] (Song& s) {
                std::vector<Clip> one { clip };
                trackclips::materialize (s, one, carried); // (round 190ne: a copied track clip comes back as a copy of its own)
                session::putClip (s, one.front(), track, scene);
            });
        }
        repaint();
    }
    void duplicateSlot (int track, int scene)
    {
        if (int t0, s0, t1, s1; rangeAround (track, scene, t0, s0, t1, s1)) // round 190lx (DL-29): the rectangle, under it
        {
            auto at = -1;
            engine.editSong ([&] (Song& s) { at = session::duplicateBlock (s, t0, s0, t1, s1); });
            if (at >= 0)
                selectRectangle (t0, at, t1, at + (s1 - s0));
            repaint();
            return;
        }
        auto to = -1;
        engine.editSong ([&] (Song& s) { to = session::duplicate (s, track, scene); });
        if (to >= 0)
        {
            selectedScene = to;
            revealSelection();
            notify (GridPick::select);
        }
        repaint();
    }
    void insertEmptyClip (int track, int scene)
    {
        engine.editSong ([=] (Song& s) { session::insertEmptyClip (s, track, scene); });
        repaint();
    }

    // The clip's groove (round 190bv): none, one of the project's grooves, or one added to the pool now.
    juce::PopupMenu grooveMenu (int track, int scene, std::uint32_t current)
    {
        juce::PopupMenu menu;
        const auto set = [this, track, scene] (std::uint32_t id) {
            engine.editSong ([=] (Song& s) { groove::setClipGroove (s, track, scene, id); });
            repaint();
        };
        menu.addItem (tr ("None"), true, current == 0, [set] { set (0); });
        for (const auto& gr : engine.getSong().grooves.grooves)
            menu.addItem (utf8 (gr.name), true, current == gr.id, [set, id = gr.id] { set (id); });
        menu.addSeparator();
        juce::PopupMenu add;
        addGrooveMenuItems (add, engine, this, [safe = juce::Component::SafePointer<ClipGridComponent> (this), track, scene] (std::uint32_t id) {
            if (safe == nullptr)
                return;
            safe->engine.editSong ([=] (Song& s) { groove::setClipGroove (s, track, scene, id); }, false);
            safe->repaint();
        });
        menu.addSubMenu (tr ("Add a groove"), add);
        menu.addItem (tr ("Show the Groove Pool"), [this] { showGroovePool (true); });
        return menu;
    }

    void showSlotMenu (int track, int scene)
    {
        buildSlotMenu (track, scene).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void addLaunchOptions (juce::PopupMenu& menu, int track, int scene)
    {
        const auto* grid = gridOf();
        if (grid == nullptr || launcher::clipAt (*grid, track, scene) == nullptr)
            return;
        auto copy = *grid;
        const auto* slot = launcher::settingsAt (copy, track, scene);
        juce::PopupMenu quant;
        for (const auto& [q, name] : quantNames)
            quant.addItem ((q == LaunchQuant::global || q <= LaunchQuant::bar) ? tr (name) : juce::String (name), true, slot->quant == q, [this, track, scene, q = q] {
                editSlot (track, scene, [q] (ClipGridSlot& s) { s.quant = q; });
            });
        menu.addSubMenu (tr ("Launch quantisation"), quant);
        juce::PopupMenu modes;
        for (const auto& [m, name] : modeNames)
            modes.addItem (tr (name), true, slot->mode == m, [this, track, scene, m = m] { editSlot (track, scene, [m] (ClipGridSlot& s) { s.mode = m; }); });
        menu.addSubMenu (tr ("Launch mode"), modes);
        menu.addItem (tr ("Legato (keep the position)"), true, slot->legato, [this, track, scene, on = slot->legato] {
            editSlot (track, scene, [on] (ClipGridSlot& s) { s.legato = ! on; });
        });
        menu.addItem (tr ("Loop"), true, slot->loop, [this, track, scene, on = slot->loop] {
            editSlot (track, scene, [on] (ClipGridSlot& s) { s.loop = ! on; });
        });
    }

    // the clip's groove items (round 190ld: one helper for both looks): the Groove submenu and Commit, and Extract Groove(s)
    void addGrooveItems (juce::PopupMenu& menu, int track, int scene, bool grooveAndCommit, bool extract)
    {
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        auto copy = *grid;
        const auto* slot = launcher::settingsAt (copy, track, scene);
        if (slot == nullptr)
            return;
        // grooves play on the notes of MIDI clips (round 190bv); round 190kc: Commit and Extract on audio clips too
        // (Live's manual 14.1.2 / 14.2.1: warp markers at the attacks; the attacks' timing and levels)
        if (const auto* clip = launcher::clipAt (*grid, track, scene); clip != nullptr && clip->kind == ClipKind::pattern)
        {
            if (grooveAndCommit)
            {
                menu.addSubMenu (tr ("Groove"), grooveMenu (track, scene, slot->groove));
                menu.addItem (tr ("Commit groove"), slot->groove != 0, false, [this, track, scene] {
                    engine.editSong ([=] (Song& s) { groove::commit (s, track, scene); });
                    repaint();
                });
            }
            if (extract)
            menu.addItem (tr ("Extract Groove(s)"), [this, track, scene] {
                engine.editSong ([=] (Song& s) {
                    const auto* c = s.clipGrid ? launcher::clipAt (*s.clipGrid, track, scene) : nullptr;
                    if (c == nullptr || c->pattern < 0 || c->pattern >= (int) s.patterns.size())
                        return;
                    const auto& pattern = s.patterns[(std::size_t) c->pattern];
                    groove::add (s.grooves, groove::extract (pattern, c->length, GrooveBase::sixteenth, pattern.name));
                });
                showGroovePool (true);
            });
        }
        else if (clip != nullptr && clip->kind == ClipKind::audio && clip->channel >= 0 && clip->channel < (int) engine.getSong().channels.size())
        {
            const auto& channel = engine.getSong().channels[(std::size_t) clip->channel];
            const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (channel.samplePath.c_str()));
            if (grooveAndCommit)
            {
                menu.addSubMenu (tr ("Groove"), grooveMenu (track, scene, slot->groove));
                menu.addItem (tr ("Commit groove"), slot->groove != 0 && sample != nullptr, false, [this, track, scene, sample] {
                    engine.editSong ([=] (Song& s) { grooveaudio::commit (s, track, scene, *sample); });
                    repaint();
                });
            }
            const auto name = juce::File (juce::String::fromUTF8 (channel.samplePath.c_str())).getFileNameWithoutExtension().toStdString();
            if (extract)
            menu.addItem (tr ("Extract Groove(s)"), sample != nullptr, false, [this, track, scene, sample, name] {
                engine.editSong ([=] (Song& s) {
                    if (const auto* c = s.clipGrid ? launcher::clipAt (*s.clipGrid, track, scene) : nullptr)
                        if (auto g = grooveaudio::extract (s, *c, *sample, GrooveBase::sixteenth, name); ! g.points.empty())
                            groove::add (s.grooves, std::move (g));
                });
                showGroovePool (true);
            });
        }
    }

    void addExtractGroove (juce::PopupMenu& menu, int track, int scene) { addGrooveItems (menu, track, scene, false, true); }

    // LYNX's own clip options after Live's items (round 190ld): a pattern into the slot, launch settings, follow action, groove
    void addClipOptions (juce::PopupMenu& menu, int track, int scene, bool)
    {
        const auto& song = engine.getSong();
        juce::PopupMenu patterns;
        for (int p = 0; p < (int) song.patterns.size(); ++p)
            patterns.addItem (utf8 (song.patterns[(std::size_t) p].name), [this, track, scene, p] { putPattern (track, scene, p); });
        menu.addSubMenu (tr ("Put a pattern here"), patterns);
        addLaunchOptions (menu, track, scene);
        if (const auto* grid = gridOf())
        {
            auto copy = *grid;
            if (const auto* slot = launcher::settingsAt (copy, track, scene))
                menu.addSubMenu (tr ("Follow action"), followMenu (track, scene, slot->follow));
        }
        addGrooveItems (menu, track, scene, true, false);
    }

    // round 190nw (LYNX's own): a pattern clip's notes in the tracker editor, on that clip's pattern
    void addEditInTracker (juce::PopupMenu& menu, int track, int scene)
    {
        const auto* grid = gridOf();
        const auto* clip = grid != nullptr ? launcher::clipAt (*grid, track, scene) : nullptr;
        if (clip == nullptr || clip->kind != ClipKind::pattern || clip->pattern < 0 || clip->pattern >= (int) engine.getSong().patterns.size())
            return;
        menu.addItem (tr ("Edit in tracker"), [this, pattern = clip->pattern] { openTracker (engine, pattern); });
    }

public:
    juce::PopupMenu buildSlotMenu (int track, int scene)
    {
        juce::PopupMenu menu;
        const auto* grid = gridOf();
        if (grid == nullptr)
            return menu;
        const auto& song = engine.getSong();
        // Round 190ld (Live 12.4's clip menu, details2_190ld shots/ref/live-clip-menu.png): in Live's look a clip's menu
        // starts with Live's items in Live's order and groups (Cut / Copy / Duplicate / Delete; Rename / Extract Groove(s) /
        // Deactivate Clip; the colour last), then LYNX's own options after a separator (add, never remove)
        if (liveLook() && launcher::clipAt (*grid, track, scene) != nullptr)
        {
            auto copy = *grid;
            const auto* slot = launcher::settingsAt (copy, track, scene);
            menu.addItem (tr ("Cut") + "\tCtrl+X", [this, track, scene] { copySlot (track, scene, true); });
            menu.addItem (tr ("Copy") + "\tCtrl+C", [this, track, scene] { copySlot (track, scene, false); });
            menu.addItem (tr ("Duplicate") + "\tCtrl+D", [this, track, scene] { duplicateSlot (track, scene); });
            menu.addItem (tr ("Delete") + "\tDel", [this, track, scene] {
                engine.editSong ([=] (Song& s) { launcher::clearClip (s, track, scene); });
                repaint();
            });
            menu.addSeparator();
            menu.addItem (tr ("Rename") + sessionkeys::menuKey (sessionkeys::Command::gridRename), [this, track, scene] { renameClip (track, scene); });
            addExtractGroove (menu, track, scene);
            menu.addItem ((slot->active ? tr ("Deactivate Clip") : tr ("Activate Clip")) + sessionkeys::menuKey (sessionkeys::Command::gridDeactivateClip),
                          [this, track, scene] { toggleClipActive (track, scene); });
            menu.addSeparator();
            // round gap190og (detail-020): Live's Crop Clip, its own group before the colours [SHOT
            // details2_190ld/shots/ref/live-clip-menu.png "Crop Clip  Ctrl+Shift+J"]; MIDI clips (an audio clip's is Crop Sample)
            if (canCropClip (track, scene))
            {
                menu.addItem (tr ("Crop Clip") + sessionkeys::menuKey (sessionkeys::Command::gridCropClip), [this, track, scene] { cropClip (track, scene); });
                menu.addSeparator();
            }
            menu.addSubMenu (tr ("Change color"), ui::colourMenu (slot->colour, [this, track, scene] (std::uint32_t c) {
                editSlot (track, scene, [c] (ClipGridSlot& s) { s.colour = c; });
            }, mouseArea()));
            menu.addSeparator();
            addClipOptions (menu, track, scene, false);
            addEditInTracker (menu, track, scene);
            menu.addItem (tr ("Paste") + "\tCtrl+V", canPaste(), false, [this, track, scene] { pasteSlot (track, scene); });
            menu.addSeparator();
            addGridOptions (menu);
            return menu;
        }
        juce::PopupMenu patterns;
        for (int p = 0; p < (int) song.patterns.size(); ++p)
            patterns.addItem (utf8 (song.patterns[(std::size_t) p].name), [this, track, scene, p] { putPattern (track, scene, p); });
        menu.addSubMenu (tr ("Put a pattern here"), patterns);

        if (launcher::clipAt (*grid, track, scene) != nullptr)
        {
            auto copy = *grid;
            const auto* slot = launcher::settingsAt (copy, track, scene);
            addLaunchOptions (menu, track, scene);
            // round 190im (R12, DL-15): Live's Deactivate / Activate Clip (key 0 in the Clip-launch layout)
            menu.addItem ((slot->active ? tr ("Deactivate Clip") : tr ("Activate Clip")) + sessionkeys::menuKey (sessionkeys::Command::gridDeactivateClip),
                          [this, track, scene] { toggleClipActive (track, scene); });
            menu.addSubMenu (tr ("Follow action"), followMenu (track, scene, slot->follow));
            // round 190kn (DL-24 / DL-25): Live's clip Rename (Ctrl+R) and the clip's colour, from the clip's own menu
            menu.addItem (tr ("Rename") + sessionkeys::menuKey (sessionkeys::Command::gridRename), [this, track, scene] { renameClip (track, scene); });
            menu.addSubMenu (tr ("Change color"), ui::colourMenu (slot->colour, [this, track, scene] (std::uint32_t c) {
                editSlot (track, scene, [c] (ClipGridSlot& s) { s.colour = c; });
            }, mouseArea()));
            addGrooveItems (menu, track, scene, true, true);
            addEditInTracker (menu, track, scene);
            menu.addSeparator();
            menu.addItem (tr ("Cut") + "\tCtrl+X", [this, track, scene] { copySlot (track, scene, true); });
            menu.addItem (tr ("Copy") + "\tCtrl+C", [this, track, scene] { copySlot (track, scene, false); });
            menu.addItem (tr ("Paste") + "\tCtrl+V", canPaste(), false, [this, track, scene] { pasteSlot (track, scene); });
            menu.addItem (tr ("Duplicate") + "\tCtrl+D", [this, track, scene] { duplicateSlot (track, scene); });
            menu.addItem (tr ("Delete") + "\tDel", [this, track, scene] { // (round 190kn, DL-26: Live's shortcut column)
                engine.editSong ([=] (Song& s) { launcher::clearClip (s, track, scene); });
                repaint();
            });
        }
        else
        {
            menu.addItem (tr ("Paste") + "\tCtrl+V", canPaste(), false, [this, track, scene] { pasteSlot (track, scene); });
            menu.addItem (tr ("Insert empty MIDI clip") + sessionkeys::menuKey (sessionkeys::Command::gridNewMidiClip),
                          [this, track, scene] { insertEmptyClip (track, scene); });
            const auto has = launcher::hasStopButton (*grid, track, scene);
            menu.addItem (tr ("Stop button (a scene stops this track here)"), true, has, [this, track, scene, has] {
                engine.editSong ([=] (Song& s) { launcher::setStopButton (s, track, scene, ! has); });
                repaint();
            });
        }
        menu.addSeparator();
        addGridOptions (menu);
        return menu;
    }

    // round gap190og (detail-020): Live's Crop Clip (core/ClipLoopOps.h): the MIDI clip's notes outside its loop (Start..End
    // with Loop off) go and the kept span starts its pattern; one undo step
    bool canCropClip (int track, int scene) const
    {
        const auto& song = engine.getSong();
        const auto* grid = gridOf();
        const auto* c = grid != nullptr ? launcher::clipAt (*grid, track, scene) : nullptr;
        return c != nullptr && cliploop::canEdit (*c, song);
    }
    void cropClip (int track, int scene)
    {
        engine.editSong ([track, scene] (Song& s) {
            if (! s.clipGrid)
                return;
            for (auto& c : s.clipGrid->clips)
                if (c.track == track && c.length > 0 && c.gridSlot >= 0 && c.gridSlot < (int) s.clipGrid->slots.size()
                    && s.clipGrid->slots[(std::size_t) c.gridSlot].scene == scene)
                {
                    cliploop::cropClip (s, c);
                    return;
                }
        });
        repaint();
    }

private:
    juce::PopupMenu followMenu (int track, int scene, const ClipFollow& f)
    {
        const auto edit = [this, track, scene] (std::function<void (ClipFollow&)> fn) {
            editSlot (track, scene, [fn] (ClipGridSlot& s) { fn (s.follow); });
        };
        return followMenuFor (track, scene, f, edit);
    }

    // round 190lr (DL-14): a scene's follow action (Live: the Scene View's Follow Action controls; no Linked mode for a
    // scene, its time counts from the scene's start)
    juce::PopupMenu sceneFollowMenu (int scene)
    {
        const auto* grid = gridOf();
        static const ClipFollow none;
        const auto& f = grid != nullptr && scene >= 0 && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene].follow : none;
        const auto edit = [this, scene] (std::function<void (ClipFollow&)> fn) {
            engine.editSong ([scene, fn] (Song& s) {
                auto& g = launcher::ensureGrid (s);
                while ((int) g.scenes.size() <= scene)
                    g.scenes.emplace_back();
                fn (g.scenes[(std::size_t) scene].follow);
            });
            repaint();
        };
        return followMenuFor (-1, scene, f, edit);
    }

    // the follow action items of a clip (track >= 0) or a scene (track −1)
    juce::PopupMenu followMenuFor (int track, int scene, const ClipFollow& f, std::function<void (std::function<void (ClipFollow&)>)> edit)
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("On"), true, f.enabled, [edit, on = f.enabled] { edit ([on] (ClipFollow& x) { x.enabled = ! on; }); });
        const auto* grid = gridOf();
        const auto actions = [&] (bool isA) {
            juce::PopupMenu sub;
            const auto current = isA ? f.a : f.b;
            for (const auto& [k, name] : followNames)
                sub.addItem (tr (name), true, current == k, [edit, isA, k = k] { edit ([isA, k] (ClipFollow& x) { (isA ? x.a : x.b) = k; }); });
            // jump: to a row of this column that has a clip (a scene: to any other scene)
            for (int r = 0; grid != nullptr && r < (int) grid->scenes.size(); ++r)
                if (r != scene && (track < 0 || launcher::clipAt (*grid, track, r) != nullptr))
                    sub.addItem (tr ("Jump to scene {0}", { juce::String (r + 1) }), true, current == FollowKind::jump && (isA ? f.jumpA : f.jumpB) == r, [edit, isA, r] {
                        edit ([isA, r] (ClipFollow& x) {
                            (isA ? x.a : x.b) = FollowKind::jump;
                            (isA ? x.jumpA : x.jumpB) = r;
                        });
                    });
            return sub;
        };
        menu.addSubMenu (tr ("Action A"), actions (true));
        menu.addSubMenu (tr ("Action B"), actions (false));
        juce::PopupMenu chance;
        for (const auto a : { 100, 75, 50, 25, 0 })
            chance.addItem (juce::String (a) + " / " + juce::String (100 - a), true, f.chanceA == a && f.chanceB == 100 - a, [edit, a] {
                edit ([a] (ClipFollow& x) {
                    x.chanceA = a;
                    x.chanceB = 100 - a;
                });
            });
        menu.addSubMenu (tr ("Chance A / B"), chance);
        juce::PopupMenu after;
        for (const auto n : { 1, 2, 4, 8 })
            if (track >= 0) // (a scene has no Linked mode)
            after.addItem (tr ("{0} passes of the clip", { juce::String (n) }), true, f.linked && f.iterations == n, [edit, n] {
                edit ([n] (ClipFollow& x) {
                    x.linked = true;
                    x.iterations = n;
                });
            });
        for (const auto bars : { 1, 2, 4, 8 })
        {
            const auto ticks = (std::int64_t) bars * engine.getSong().barTicks();
            after.addItem (tr ("{0} bars", { juce::String (bars) }), true, (track < 0 || ! f.linked) && f.ticks == ticks, [edit, ticks] {
                edit ([ticks] (ClipFollow& x) {
                    x.linked = false;
                    x.ticks = ticks;
                });
            });
        }
        menu.addSubMenu (tr ("After"), after);
        return menu;
    }

    // Freeze (round 190ay): a column is a Playlist track; its instrument is the channel its chain shows (core/DetailChain.h)
    static int frozenChannelOf (const Song& song, int track)
    {
        const auto channel = detailchain::forTrack (song, track).channel;
        return channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].freeze.on() ? channel : -1;
    }

    void showHeaderMenu (int track)
    {
        buildHeaderMenu (track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    static juce::Rectangle<int> mouseArea()
    {
        const auto p = juce::Desktop::getMousePosition();
        return { p.x, p.y, 1, 1 };
    }

public:
    // Freeze Track / Unfreeze Track (round 190ff, the reference's Freeze Track: the track's instrument with its volume
    // and pan through its insert's effects; core/StandIns.h lynx::freeze track freeze)
    void toggleTrackFreeze (int track)
    {
        const auto& song = engine.getSong();
        const auto channel = detailchain::forTrack (song, track).channel;
        if (channel < 0 || channel >= (int) song.channels.size())
            return;
        if (song.channels[(std::size_t) channel].freeze.on())
        {
            unfreezeChannel (engine, channel);
            repaint();
            return;
        }
        freezeTrack (engine, channel, [safe = juce::Component::SafePointer<juce::Component> (this)] (bool) {
            if (safe != nullptr)
                safe->repaint();
        });
    }

public:
    // A column title's menu: Rename and the colour (round 190ef, the reference's track title menu); the reference's
    // Freeze Track / Unfreeze Track and Bounce Track in Place (round 190ff; round 190ay had the channel freeze here).
    juce::PopupMenu buildHeaderMenu (int track)
    {
        const auto& song = engine.getSong();
        const auto channel = detailchain::forTrack (song, track).channel;
        const auto valid = channel >= 0 && channel < (int) song.channels.size();
        juce::PopupMenu menu;
        menu.addItem (tr ("Rename") + sessionkeys::menuKey (sessionkeys::Command::gridRename), [this, track] { renameTrack (track); });
        menu.addSeparator();
        const auto frozen = valid && song.channels[(std::size_t) channel].freeze.on();
        menu.addItem ((frozen ? tr ("Unfreeze Track") : tr ("Freeze Track")) + sessionkeys::menuKey (sessionkeys::Command::freezeTrack), valid, false,
                      [this, track] { toggleTrackFreeze (track); });
        // (LYNX's own: a frozen track whose audio no longer matches is rendered again without unfreezing first)
        if (frozen && (engine.freezeOutOfDate (channel) || engine.frozenAudioMissing (channel)))
            menu.addItem (tr ("Re-freeze"), [this, channel] {
                freezeTrack (engine, channel, [safe = juce::Component::SafePointer<juce::Component> (this)] (bool) {
                    if (safe != nullptr)
                        safe->repaint();
                });
            });
        menu.addItem (tr ("Bounce Track in Place"), valid, false, [this, channel] {
            bounceTrackInPlace (engine, channel);
            repaint();
        });
        // round 190op (P4, LYNX extras; owner decision O-3): Deactivate Track unloads the track's devices (the Track
        // Activator stays Live's mute); Hide takes the column out of view (Show hidden tracks, Playlist menu)
        {
            const auto units = trackstates::unitsOfTrack (song, track);
            const auto off = trackstates::trackDeactivated (song, units);
            menu.addItem (off ? tr ("Reactivate Track") : tr ("Deactivate Track"), ! units.empty(), off, [this, track, off] {
                const auto u = trackstates::unitsOfTrack (engine.getSong(), track);
                engine.setDeactivated (u.channels, u.inserts, ! off);
                repaint();
            });
            const auto hidden = trackstates::isHidden (song, track);
            menu.addItem (tr ("Hide Track"), true, hidden, [this, track, hidden] {
                engine.editSong ([track, hidden] (Song& s) { trackstates::setHidden (s, { track }, ! hidden); });
                repaint();
            });
        }
        menu.addSeparator();
        const auto colour = track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].colour : 0u;
        menu.addSubMenu (tr ("Change color"), ui::colourMenu (colour, [this, track] (std::uint32_t c) {
            engine.editSong ([track, c] (Song& s) {
                if ((int) s.playlistTracks.size() <= track)
                    s.playlistTracks.resize ((std::size_t) track + 1);
                s.playlistTracks[(std::size_t) track].colour = c;
            });
            repaint();
        }, mouseArea()));
        return menu;
    }

    // A scene's menu (round 190ef adds the reference's Duplicate, Capture and Insert Scene and the colour).
    juce::PopupMenu buildSceneMenu (int scene)
    {
        if (liveLook())
            return buildSceneMenuLive (scene);
        juce::PopupMenu menu;
        // round 190kn (DL-7): Live's "Cancel Scene Launch" (manual 7.2: "clicking the Cancel Scene Launch entry in the Main
        // track's context menu"); here the scene column is Live's Main track. Enabled while a launched scene waits.
        menu.addItem (tr ("Cancel Scene Launch"), queuedScene >= 0, false, [this] { cancelSceneLaunch(); });
        menu.addSeparator();
        menu.addItem (tr ("Insert scene") + sessionkeys::menuKey (sessionkeys::Command::gridInsertScene), [this, scene] {
            engine.editSong ([scene] (Song& s) { launcher::insertScene (s, scene); });
            clampScroll();
            repaint();
        });
        menu.addItem (tr ("Add scene below"), [this, scene] { insertSceneBelow (scene); });
        // (round 190em: also with nothing playing, as the reference: an empty scene like this one)
        menu.addItem (tr ("Capture and insert scene") + sessionkeys::menuKey (sessionkeys::Command::gridCaptureInsertScene),
                      [this, scene] { captureAndInsertScene (scene); });
        menu.addItem (tr ("Duplicate") + "\tCtrl+D", [this, scene] { duplicateScene (scene); });
        const auto* grid = gridOf();
        menu.addItem (tr ("Delete scene"), grid != nullptr && grid->scenes.size() > 1, false, [this, scene] { deleteScene (scene); });
        menu.addSeparator();
        menu.addItem (tr ("Rename scene...") + sessionkeys::menuKey (sessionkeys::Command::gridRename), [this, scene] { renameScene (scene); });
        static const ClipGridScene unnamed;
        const auto& info = grid != nullptr && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene] : unnamed;
        menu.addSubMenu (tr ("Change color"), ui::colourMenu (info.colour, [this, scene] (std::uint32_t c) {
            engine.editSong ([scene, c] (Song& s) {
                auto& g = launcher::ensureGrid (s);
                while ((int) g.scenes.size() <= scene)
                    g.scenes.emplace_back();
                g.scenes[(std::size_t) scene].colour = c;
            });
            repaint();
        }, mouseArea()));
        const auto tempo = engine.getSong().bpm;
        menu.addItem (info.tempo > 0.0 ? tr ("Scene tempo: {0} (remove)", { juce::String (info.tempo, 2) })
                                       : tr ("Scene tempo: take {0} when launched", { juce::String (tempo, 2) }),
                      [this, scene, tempo, had = info.tempo > 0.0] {
                          engine.editSong ([=] (Song& s) { session::setSceneTempo (s, scene, had ? 0.0 : tempo); });
                      });
        if (info.beatsPerBar > 0)
            menu.addItem (tr ("Disable scene time signature"), [this, scene] { engine.editSong ([scene] (Song& s) { session::setSceneBeats (s, scene, 0); }); });
        menu.addSubMenu (tr ("Follow Action"), sceneFollowMenu (scene)); // round 190lr (DL-14)
        menu.addSeparator();
        addGridOptions (menu);
        return menu;
    }

    // Round 190ld: Live 12.4's Main track menu (details2_190ld shots/ref/live-scene-menu-queued.png) in Live's look — its
    // items in Live's order and groups (Duplicate / Delete; Rename; Insert Scene / Capture and Insert Scene; Cancel Scene
    // Launch; the colour), then LYNX's own scene items and the grid options after a separator (add, never remove)
    juce::PopupMenu buildSceneMenuLive (int scene)
    {
        juce::PopupMenu menu;
        const auto* grid = gridOf();
        menu.addItem (tr ("Duplicate") + "\tCtrl+D", [this, scene] { duplicateScene (scene); });
        menu.addItem (tr ("Delete"), grid != nullptr && grid->scenes.size() > 1, false, [this, scene] { deleteScene (scene); });
        menu.addSeparator();
        menu.addItem (tr ("Rename") + sessionkeys::menuKey (sessionkeys::Command::gridRename), [this, scene] { renameScene (scene); });
        menu.addSeparator();
        menu.addItem (tr ("Insert Scene") + sessionkeys::menuKey (sessionkeys::Command::gridInsertScene), [this, scene] {
            engine.editSong ([scene] (Song& s) { launcher::insertScene (s, scene); });
            clampScroll();
            repaint();
        });
        menu.addItem (tr ("Capture and Insert Scene") + sessionkeys::menuKey (sessionkeys::Command::gridCaptureInsertScene),
                      [this, scene] { captureAndInsertScene (scene); });
        menu.addSeparator();
        menu.addItem (tr ("Cancel Scene Launch"), [this] { cancelSceneLaunch(); }); // (Live: enabled whether or not a scene waits)
        menu.addSeparator();
        menu.addItem (tr ("Select Next Scene on Launch (Pref)"), true, selectNextOnLaunch, [this] {
            selectNextOnLaunch = ! selectNextOnLaunch;
            if (onSceneFieldsChanged != nullptr)
                onSceneFieldsChanged(); // (saved with the UI settings)
        });
        menu.addItem (tr ("Trigger Recording on Launch (Pref)"), true, recordOnSceneLaunch, [this] {
            recordOnSceneLaunch = ! recordOnSceneLaunch;
            if (onSceneFieldsChanged != nullptr)
                onSceneFieldsChanged();
        });
        menu.addSeparator();
        static const ClipGridScene unnamed;
        const auto& info = grid != nullptr && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene] : unnamed;
        menu.addSubMenu (tr ("Change color"), ui::colourMenu (info.colour, [this, scene] (std::uint32_t c) {
            engine.editSong ([scene, c] (Song& s) {
                auto& g = launcher::ensureGrid (s);
                while ((int) g.scenes.size() <= scene)
                    g.scenes.emplace_back();
                g.scenes[(std::size_t) scene].colour = c;
            });
            repaint();
        }, mouseArea()));
        menu.addSeparator();
        menu.addItem (tr ("Add scene below"), [this, scene] { insertSceneBelow (scene); });
        const auto tempo = engine.getSong().bpm;
        menu.addItem (info.tempo > 0.0 ? tr ("Scene tempo: {0} (remove)", { juce::String (info.tempo, 2) })
                                       : tr ("Scene tempo: take {0} when launched", { juce::String (tempo, 2) }),
                      [this, scene, tempo, had = info.tempo > 0.0] {
                          engine.editSong ([=] (Song& s) { session::setSceneTempo (s, scene, had ? 0.0 : tempo); });
                      });
        if (info.beatsPerBar > 0)
            menu.addItem (tr ("Disable scene time signature"), [this, scene] { engine.editSong ([scene] (Song& s) { session::setSceneBeats (s, scene, 0); }); });
        menu.addSubMenu (tr ("Follow Action"), sceneFollowMenu (scene)); // round 190lr (DL-14)
        menu.addSeparator();
        addGridOptions (menu);
        return menu;
    }

    // --- round 190ef: scene work ---------------------------------------------------------------------------------------
    // --details190imshot (round 190im): a scene launched as its button launches it; the grid's timer tick; the cell
    // rectangles (0 a slot, 1 a track's status cell, 2 a scene cell); the selection
    void launchSceneForSnapshot (int scene) { launchScene (scene); }
    // Round 190kc: Live's Relative Session Mapping Strip — the highlighted (selected) scene moves up / down (Live keeps it
    // in view), is launched (and the one below selected, as its button does), or a track's clip in it is launched
    void relativeSession (RemoteAction action, int track, bool press)
    {
        switch (action)
        {
            case RemoteAction::sceneUp:
            case RemoteAction::sceneDown:
                if (! press)
                    return;
                selectedScene = juce::jlimit (0, rowCount() - 1, selectedScene + (action == RemoteAction::sceneUp ? -1 : 1));
                sceneSelected = true;
                titleSelected = false;
                selectedField = 0;
                revealSelection();
                repaint();
                notify (GridPick::select);
                return;
            case RemoteAction::launchSelectedScene:
                if (press)
                    launchScene (juce::jlimit (0, rowCount() - 1, selectedScene));
                return;
            case RemoteAction::launchSelectedClip: // (a press and a release: the slot's launch mode, as a mapped slot)
                engine.launcherCommand ({ press ? launcher::Command::Type::launch : launcher::Command::Type::release, track, selectedScene });
                return;
            default:
                return;
        }
    }
    void tickForSnapshot() { timerCallback(); }
    std::vector<int> columnTracksForSnapshot() const { return columnMap().columnTracks; }
    juce::Rectangle<int> cellForSnapshot (int kind, int track, int scene) const
    {
        const auto cells = cellArea();
        if (kind == 1)
            return { columnX (track), cells.getBottom(), columnWidth, statusHeight };
        if (kind == 2)
            return { sceneX(), rowY (scene), sceneWidthNow(), rowHeight };
        return { columnX (track), rowY (scene), columnWidth, rowHeight };
    }
    std::pair<int, bool> sceneSelectionForSnapshot() const { return { selectedScene, sceneSelected }; }
    // hotfix 0.1.31.1: a track chosen in the Arrangement (its title, a clip or a lane clicked) is the selected track here
    // too, as a click on its column title: Live keeps one selected track for both views, and the Device View shows it
    void selectTrackFromArrangement (int t)
    {
        if (t < 0 || t >= launcher::maxTracks)
            return;
        trackSet.clear();
        selectedTrack = t;
        sceneSelected = false;
        titleSelected = true;
        repaint();
        if (mixer != nullptr)
            mixer->repaint();
        notify (GridPick::select);
    }
    void selectSlotForSnapshot (int track, int scene)
    {
        selectedTrack = track;
        selectedScene = scene;
        sceneSelected = titleSelected = false;
        repaint();
    }

    void selectSceneForSnapshot (int scene, int field = 0)
    {
        selectedScene = scene;
        sceneSelected = true;
        titleSelected = false;
        selectedField = field;
        repaint();
    }
    // Capture and Insert Scene with the given per-track playing rows (--sceneshot190ef: nothing plays off-screen)
    int captureSceneForSnapshot (int scene, const std::vector<int>& playing) { return captureAndInsertScene (scene, &playing); }
    int duplicateSceneForSnapshot (int scene) { return duplicateScene (scene); }

private:
    void showSceneMenu (int scene)
    {
        buildSceneMenu (scene).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void selectNewScene (int scene)
    {
        if (scene < 0)
            return;
        selectedScene = scene;
        sceneSelected = true;
        titleSelected = false;
        clampScroll();
        revealSelection();
        repaint();
    }

    void insertSceneBelow (int scene)
    {
        auto at = -1;
        engine.editSong ([scene, &at] (Song& s) { at = launcher::insertScene (s, scene + 1); });
        selectNewScene (at);
    }

    int duplicateScene (int scene)
    {
        auto at = -1;
        engine.editSong ([scene, &at] (Song& s) { at = session::duplicateScene (s, scene); });
        selectNewScene (at);
        return at;
    }

    void deleteScene (int scene)
    {
        const auto* grid = gridOf();
        if (grid == nullptr || grid->scenes.size() <= 1 || scene >= (int) grid->scenes.size())
            return;
        engine.editSong ([scene] (Song& s) { launcher::removeScene (s, scene); });
        selectedScene = juce::jmax (0, selectedScene - (selectedScene >= scene ? 1 : 0));
        clampScroll();
        repaint();
    }

    // the row each track plays now (−1: none)
    std::vector<int> playingRows() const
    {
        std::vector<int> rows;
        if (const auto* state = engine.launcherState())
            for (int t = 0, n = columnCount(); t < n; ++t)
                rows.push_back (state->tracks[(std::size_t) t].playing.load (std::memory_order_relaxed));
        return rows;
    }

    // round 190em (Live 12.4.16, live check 190ei): clips play: a plain new scene with their copies, and playback moves
    // to it seamlessly (the playing slots take the new row; the transport runs on); nothing plays: an empty scene like the
    // selected one (name, tempo, signature). Either way the new scene is selected.
    int captureAndInsertScene (int scene, const std::vector<int>* given = nullptr)
    {
        const auto playing = given != nullptr ? *given : playingRows();
        auto at = -1;
        engine.editSong ([&] (Song& s) { at = session::captureScene (s, scene, playing); });
        selectNewScene (at);
        return at;
    }

    // round 190ef: the scene fields (drag, type, disable)
    void beginSceneFieldDrag (int scene, int field)
    {
        const auto* grid = gridOf();
        const auto& song = engine.getSong();
        static const ClipGridScene unnamed;
        const auto& info = grid != nullptr && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene] : unnamed;
        fieldScene = scene;
        fieldKind = field;
        fieldUndoTaken = false;
        const auto on = info.beatsPerBar > 0;
        const auto beats = on ? info.beatsPerBar : song.beatsPerBar, unit = on ? info.beatUnit : 4;
        fieldStart = field == 1 ? (info.tempo > 0.0 ? info.tempo : song.bpm) : field == 2 ? (double) beats : (double) unit;
        fieldOtherPart = field == 2 ? unit : beats;
    }
    void dragSceneField (const juce::MouseEvent& e)
    {
        // round 190em: the reference's rates (live check 190ei): tempo ≈ 2.8 BPM per pixel in whole BPM, the numerator
        // ≈ 4.6 per pixel, the denominator one step (1, 2, 4, 8, 16) per 10 px; a drag on a field that is off turns it
        // on. Ctrl on the tempo: a hundredth per pixel (LYNX's fine drag).
        const auto dy = (double) -e.getDistanceFromDragStartY();
        const auto scene = fieldScene;
        const auto undoable = ! fieldUndoTaken;
        fieldUndoTaken = true;
        if (fieldKind == 1)
        {
            const auto bpm = e.mods.isCommandDown() ? fieldStart + dy * 0.01 : session::sceneTempoDragged (fieldStart, dy);
            engine.editSong ([scene, bpm] (Song& s) { session::setSceneTempo (s, scene, bpm); }, undoable);
        }
        else
        {
            const auto beats = fieldKind == 2 ? session::sceneBeatsDragged ((int) fieldStart, dy) : fieldOtherPart;
            const auto unit = fieldKind == 3 ? session::sceneUnitDragged ((int) fieldStart, dy) : fieldOtherPart;
            engine.editSong ([scene, beats, unit] (Song& s) { session::setSceneSignature (s, scene, beats, unit); }, undoable);
        }
        repaint();
    }
    void typeSceneField (int scene, int field)
    {
        const auto* grid = gridOf();
        static const ClipGridScene unnamed;
        const auto& info = grid != nullptr && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene] : unnamed;
        const auto& song = engine.getSong();
        const auto current = field == 1 ? utf8 (session::sceneTempoText (info.tempo > 0.0 ? info.tempo : song.bpm))
                                        : utf8 (info.beatsPerBar > 0 ? session::sceneSignatureText (info.beatsPerBar, info.beatUnit)
                                                                     : session::sceneSignatureText (song.beatsPerBar, song.beatUnit));
        auto* window = new juce::AlertWindow (field == 1 ? tr ("Scene tempo") : tr ("Scene time signature"),
                                              field == 1 ? juce::String ("BPM") : tr ("Beats / beat unit (e.g. 6 / 8)"), juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("value", current);
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ClipGridComponent> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, scene, field] (int result) {
            const std::unique_ptr<juce::AlertWindow> owned (window);
            if (result != 1 || self == nullptr)
                return;
            const auto typed = owned->getTextEditorContents ("value").trim();
            const auto text = typed.upToFirstOccurrenceOf ("/", false, false).trim();
            if (text.isEmpty() || ! text.containsAnyOf ("0123456789"))
                return;
            const auto unitText = typed.fromFirstOccurrenceOf ("/", false, false).trim();
            const auto unit = unitText.containsAnyOf ("0123456789") ? unitText.getIntValue() : 4;
            if (field == 1)
                self->engine.editSong ([scene, v = text.getDoubleValue()] (Song& s) { session::setSceneTempo (s, scene, v); });
            else
                self->engine.editSong ([scene, v = text.getIntValue(), unit] (Song& s) { session::setSceneSignature (s, scene, juce::jmax (1, v), unit); });
            self->repaint();
        }), false);
    }
    void showSceneFieldMenu (int scene, int field)
    {
        buildSceneFieldMenu (scene, field).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

public:
    juce::PopupMenu buildSceneFieldMenu (int scene, int field)
    {
        const auto* grid = gridOf();
        static const ClipGridScene unnamed;
        const auto& info = grid != nullptr && scene < (int) grid->scenes.size() ? grid->scenes[(std::size_t) scene] : unnamed;
        // round 190em: the reference's one item, "Return to Default   Del" (live check 190ei): the field goes off
        juce::PopupMenu menu;
        menu.addItem (tr ("Return to Default") + "\t" + tr ("Del"), field == 1 ? info.tempo > 0.0 : info.beatsPerBar > 0, false,
                      [this, scene, field] { returnSceneFieldToDefault (scene, field); });
        return menu;
    }
    void returnSceneFieldToDefault (int scene, int field)
    {
        const auto* grid = gridOf();
        if (grid == nullptr || scene < 0 || scene >= (int) grid->scenes.size())
            return;
        const auto& info = grid->scenes[(std::size_t) scene];
        if (field == 1 ? info.tempo <= 0.0 : info.beatsPerBar <= 0)
            return;
        if (field == 1)
            engine.editSong ([scene] (Song& s) { session::setSceneTempo (s, scene, 0.0); });
        else
            engine.editSong ([scene] (Song& s) { session::setSceneBeats (s, scene, 0); });
        repaint();
    }

private:
    void renameTrack (int track)
    {
        const auto& tracks = engine.getSong().playlistTracks;
        const auto current = track < (int) tracks.size() ? utf8 (tracks[(std::size_t) track].name) : juce::String();
        auto* window = new juce::AlertWindow (tr ("Rename track"), {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", current.isNotEmpty() ? current : tr ("Track {0}", { juce::String (track + 1) }));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ClipGridComponent> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, track] (int result) {
            const std::unique_ptr<juce::AlertWindow> owned (window);
            if (result == 1 && self != nullptr)
            {
                const auto name = owned->getTextEditorContents ("name").trim().toStdString();
                self->engine.editSong ([=] (Song& s) {
                    if ((int) s.playlistTracks.size() <= track)
                        s.playlistTracks.resize ((std::size_t) track + 1);
                    s.playlistTracks[(std::size_t) track].name = name;
                });
                self->repaint();
            }
        }), false);
    }
    // round 190kn (DL-24): the clip's own name (empty = the pattern's or the sample's name again)
    void renameClip (int track, int scene)
    {
        const auto* grid = gridOf();
        if (grid == nullptr)
            return;
        const auto* clip = launcher::clipAt (*grid, track, scene);
        if (clip == nullptr)
            return;
        auto* window = new juce::AlertWindow (tr ("Rename clip"), {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", clipName (engine.getSong(), *grid, *clip));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ClipGridComponent> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, track, scene] (int result) {
            const std::unique_ptr<juce::AlertWindow> owned (window);
            if (result == 1 && self != nullptr)
            {
                const auto name = owned->getTextEditorContents ("name").trim().toStdString();
                self->editSlot (track, scene, [name] (ClipGridSlot& s) {
                    s.name = name;
                    s.blankName = false;
                });
            }
        }), false);
    }

public:
    // round 190kn (DL-7): drops the launches still waiting (the launcher's cancelLaunch, as the mapping strip's item) and
    // ends the queued scene's pulse at once
    void cancelSceneLaunch()
    {
        engine.launcherCommand ({ launcher::Command::Type::cancelLaunch });
        if (queuedScene >= 0)
        {
            const auto row = queuedScene;
            queuedScene = -1;
            repaintScene (row);
        }
    }
    bool sceneLaunchQueued() const noexcept { return queuedScene >= 0; } // (tests / shots)

private:
    void renameScene (int scene)
    {
        const auto* grid = gridOf();
        if (grid == nullptr || scene < 0 || scene >= launcher::maxScenes)
            return;
        auto* window = new juce::AlertWindow (tr ("Rename scene"), {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", scene < (int) grid->scenes.size() ? utf8 (grid->scenes[(std::size_t) scene].name) : juce::String());
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ClipGridComponent> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, scene] (int result) {
            const std::unique_ptr<juce::AlertWindow> owned (window);
            if (result == 1 && self != nullptr)
            {
                const auto name = owned->getTextEditorContents ("name").toStdString();
                self->engine.editSong ([=] (Song& s) {
                    auto& g = launcher::ensureGrid (s); // (a row shown under the last scene becomes one)
                    while ((int) g.scenes.size() <= scene)
                        g.scenes.emplace_back();
                    g.scenes[(std::size_t) scene].name = name;
                });
                self->repaint();
            }
        }), false);
    }

    void addGridOptions (juce::PopupMenu& menu)
    {
        // round 190ol (research190oe P1; Live users' "markers to scenes"): one scene per section of the arrangement's
        // Sections lane, named and coloured after it, with each track's clip of that section (core/SectionOps.h)
        const auto sectionCount = (int) engine.getSong().sections().size();
        menu.addItem (tr ("Create scenes from sections") + (sectionCount > 0 ? " (" + juce::String (sectionCount) + ")" : juce::String()), sectionCount > 0, false, [this] {
            engine.editSong ([] (Song& s) { sections::createScenes (s); });
            clampScroll();
            repaint();
        });
        // round 190ef: the reference's View › Scene Tempo And Time Signature (a preference, kept with the UI settings)
        menu.addItem (tr ("Scene tempo and time signature"), true, sceneFieldsOn, [this] {
            sceneFieldsOn = ! sceneFieldsOn;
            if (onSceneFieldsChanged != nullptr)
                onSceneFieldsChanged();
            resized();
            repaint();
        });
        const auto* grid = gridOf();
        const auto on = grid != nullptr && grid->backOnStop;
        menu.addItem (tr ("Give tracks back to the Playlist when the transport stops"), true, on, [this, on] {
            engine.editSong ([on] (Song& s) { launcher::ensureGrid (s).backOnStop = ! on; }, false);
        });
        // round 190bv: a recording into a slot ends by itself after this many bars and the clip plays on
        juce::PopupMenu fixed;
        const auto bars = engine.getSlotRecordBars();
        fixed.addItem (tr ("Off"), true, bars == 0, [this] { engine.setSlotRecordBars (0); });
        for (const auto n : fixedLengths)
            fixed.addItem (n == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (n) }), true, bars == n, [this, n] { engine.setSlotRecordBars (n); });
        menu.addSubMenu (tr ("Fixed recording length"), fixed);
        // round 190dr: Live's View > Mixer and View > Mixer Controls
        juce::PopupMenu mixerItems;
        mixer->addViewItems (mixerItems);
        menu.addSubMenu (tr ("Mixer"), mixerItems);
        const auto follow = grid == nullptr || grid->followActions;
        menu.addItem (tr ("Follow actions on the whole grid"), true, follow, [this, follow] {
            engine.editSong ([follow] (Song& s) { launcher::ensureGrid (s).followActions = ! follow; }, false);
        });
    }

    template <typename Fn>
    void editSlot (int track, int scene, Fn fn)
    {
        engine.editSong ([=] (Song& s) {
            if (s.clipGrid)
                if (auto* slot = launcher::settingsAt (*s.clipGrid, track, scene))
                    fn (*slot);
        });
        repaint();
    }

    void syncControls()
    {
        if (const auto* grid = gridOf())
            globalQuant.setSelectedId ((int) grid->globalQuant + 1, juce::dontSendNotification);
        const auto* state = engine.launcherState();
        auto taken = false;
        if (state != nullptr)
            for (int t = 0, n = columnCount(); t < n && ! taken; ++t)
                taken = state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
        taken = taken || engine.getSong().sceneTempoHold; // (round 190hq: a scene's tempo holds the Playlist's tempo automation)
        sessionRecord.setToggleState (engine.isSessionRecording(), juce::dontSendNotification);
        if (backToPlaylist.getToggleState() != taken)
        {
            backToPlaylist.setToggleState (taken, juce::dontSendNotification);
            backToPlaylist.setColour (juce::TextButton::buttonOnColourId, theme::colour::warning);
            backToPlaylist.setColour (juce::TextButton::textColourOnId, theme::colour::bg1);
        }
    }

    // Only while the grid is on screen
    void timerCallback() override
    {
        syncControls();
        syncMixer();
        if (mixer != nullptr)
        {
            if (const auto want = mixerHeight(); want != mixer->getHeight())
                resized();
            mixer->tick(); // (its meters and arm buttons repaint themselves when they change)
            if (! ui::paintSavings()) // (LYNX_PAINT_SAVINGS=0: the whole mixer every tick, as before)
                mixer->repaint();
        }
        // round 190im (R6): a launched scene's pulse and its end (before the paths below that return early)
        stepQueuedScene (juce::jmax (0, juce::jmin (columnCount(), launcher::maxTracks)));
        // round 190ge: the whole grid was repainted 30 times a second, also with nothing playing. Now a change of what
        // all of it shows (the song, the transport, the layout, the selection) repaints it; otherwise only the columns
        // whose own state moved (a playing clip's progress, a blink, an arm) are repainted, header to status row
        const auto columns = juce::jmax (0, juce::jmin (columnCount(), launcher::maxTracks));
        const auto cells = cellArea();
        if (! ui::paintSavings()) // (LYNX_PAINT_SAVINGS=0: the whole grid every tick, as before)
        {
            repaint();
            return;
        }
        if (const auto now = drawnState(); now != lastDrawnState || columnStates.size() != (std::size_t) columns)
        {
            lastDrawnState = now;
            columnStates.assign ((std::size_t) columns, 0);
            columnViews.assign ((std::size_t) columns, {});
            for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
            {
                columnStates[(std::size_t) t] = columnState (t); // (what this repaint shows)
                columnViews[(std::size_t) t] = columnView (t);
            }
            repaint();
            return;
        }
        const auto visible = juce::Rectangle<int> (0, 0, cells.getRight(), getHeight());
        if (! ui::paintCache()) // (LYNX_PAINT_CACHE=0: a changed column is repainted whole, as round 190ge did)
        {
            for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
                if (const auto now = columnState (t); now != columnStates[(std::size_t) t])
                {
                    columnStates[(std::size_t) t] = now;
                    repaint (juce::Rectangle<int> (columnX (t), 0, columnWidth, cells.getBottom() + statusHeight + 1).getIntersection (visible));
                }
            return;
        }
        // round 190gr (A11): only the parts of a column that changed — the cells whose launcher state moved, a playing
        // clip's progress bar (the stretch between its last and its new length), the header for the "taken" mark, the
        // status cell for the recording counter; an arm repaints the whole column (its empty slots show it)
        if (columnViews.size() != (std::size_t) columns)
            columnViews.assign ((std::size_t) columns, {});
        for (int t = firstColumn(); t < columns && columnX (t) < cells.getRight(); t = nextShown (t))
        {
            const auto now = columnView (t);
            auto& was = columnViews[(std::size_t) t];
            const auto x = columnX (t);
            const auto whole = juce::Rectangle<int> (x, 0, columnWidth, cells.getBottom() + statusHeight + 1);
            if (! was.known || was.armed != now.armed)
            {
                repaint (whole.getIntersection (visible));
                was = now;
                continue;
            }
            const auto cellOf = [&] (int row) {
                return juce::Rectangle<int> (x, rowY (row), columnWidth, rowHeight).getIntersection (cells).getIntersection (visible);
            };
            const auto rowDirty = [&] (int row) {
                if (row >= 0)
                    repaint (cellOf (row));
            };
            if (was.taken != now.taken)
                repaint (juce::Rectangle<int> (x, 0, columnWidth, headerHeight).getIntersection (visible));
            for (const auto pair : { std::pair (was.playing, now.playing), std::pair (was.pending, now.pending), std::pair (was.recording, now.recording),
                                     std::pair (was.recordPending, now.recordPending) })
                if (pair.first != pair.second)
                {
                    rowDirty (pair.first);
                    rowDirty (pair.second);
                }
            if (was.stopping != now.stopping)
            {
                rowDirty (was.playing);
                rowDirty (now.playing);
            }
            if (was.blink != now.blink) // (what blinks: a waiting launch or record, a stop asked for)
            {
                rowDirty (now.pending);
                rowDirty (now.recordPending);
                if (now.stopping && liveLook()) // (Live: a waiting stop pulses the track's stop square, not the triangle)
                    repaint (juce::Rectangle<int> (x, cells.getBottom(), columnWidth, statusHeight + 1).getIntersection (visible));
                else if (now.stopping)
                    rowDirty (now.playing);
            }
            if (now.recording >= 0 && was.recordTick != now.recordTick) // (the take's name; the length counter)
                rowDirty (now.recording);
            if (was.recording != now.recording || (now.recording >= 0 && was.recordTick != now.recordTick))
                repaint (juce::Rectangle<int> (x, cells.getBottom(), columnWidth, statusHeight + 1).getIntersection (visible));
            if (liveLook())
            {
                // Live (CL-15): the position is the status cell's pie, redrawn when it moves by a degree; the loop count
                // and the length go with a clip starting or stopping
                const auto degree = [] (const ColumnView& v) { return v.playing >= 0 ? (int) (juce::jlimit (0.0f, 1.0f, v.progress) * 360.0f) : -1; };
                if (was.playing != now.playing || degree (was) != degree (now))
                    repaint (juce::Rectangle<int> (x, cells.getBottom(), columnWidth, statusHeight + 1).getIntersection (visible));
            }
            else if (now.playing >= 0 && now.playing == was.playing && now.progress != was.progress)
            {
                // the bar under the playing cell (paintSlot: 3 px high, cell width × progress, antialiased at its end)
                const auto cell = juce::Rectangle<int> (x, rowY (now.playing), columnWidth, rowHeight).reduced (1);
                const auto a = (float) cell.getWidth() * juce::jlimit (0.0f, 1.0f, was.progress);
                const auto b = (float) cell.getWidth() * juce::jlimit (0.0f, 1.0f, now.progress);
                const auto from = cell.getX() + (int) std::floor (juce::jmin (a, b)) - 1, to = cell.getX() + (int) std::ceil (juce::jmax (a, b)) + 1;
                repaint (juce::Rectangle<int> (from, cell.getBottom() - 3, to - from, 3).getIntersection (cell).getIntersection (cells).getIntersection (visible));
            }
            if (liveLook() && (was.oneShotSecond != now.oneShotSecond || was.miniStep != now.miniStep || was.monitor != now.monitor))
                // (round 190im, DL-9: a one-shot's time left; round 190ld: the miniature's position, the monitor icon)
                repaint (juce::Rectangle<int> (x, cells.getBottom(), columnWidth, statusHeight + 1).getIntersection (visible));
            was = now;
        }
    }

    // Round 190im (R6, DL-5): the launched scene's pulse until its clips start: the scene cell alone, on each blink phase
    // (none with Animations off: a still lit button), and once more when it ends.
    void stepQueuedScene (int columns)
    {
        if (queuedScene < 0)
            return;
        const auto* state = engine.launcherState();
        auto waiting = false;
        if (state != nullptr)
            for (int t = 0; t < columns && ! waiting; ++t)
                waiting = state->tracks[(std::size_t) t].pending.load (std::memory_order_relaxed) == queuedScene;
        queuedSeen = queuedSeen || waiting;
        ++queuedTicks;
        // (it ends once its clips started; a scene with nothing to start ends after a few ticks)
        if (! waiting && (queuedSeen || queuedTicks > 6))
        {
            const auto row = queuedScene;
            queuedScene = -1;
            repaintScene (row);
            return;
        }
        if (const auto phase = blinkPhaseNow(); phase != queuedBlink)
        {
            queuedBlink = phase;
            repaintScene (queuedScene);
        }
    }

    // What one column's paint reads of the launcher and the engine (round 190gr, A11): compared field by field with the
    // last tick, so a repaint covers only the parts that changed.
    struct ColumnView
    {
        bool known = false, armed = false, taken = false, stopping = false;
        int playing = -1, pending = -1, recording = -1, recordPending = -1;
        float progress = 0.0f;
        std::uint32_t blink = 0;      // the blink phase, while something in the column blinks
        std::uint64_t recordTick = 0; // while it records: the recording's start, the clock, a 33 ms tick
        int oneShotSecond = -1;       // round 190im (DL-9): a playing one-shot clip's seconds left
        int miniStep = -1;            // round 190ld (DL-10): the song position's step in the Arrangement miniature
        int monitor = 0;              // round 190ld (DL-11): 1 = an audio track monitoring In (mic), 2 = a MIDI one (keyboard)
    };

    ColumnView columnView (int t) const
    {
        ColumnView c;
        c.known = true;
        c.armed = engine.isGridTrackArmed (t);
        if (const auto* state = engine.launcherState())
        {
            const auto& v = state->tracks[(std::size_t) t];
            c.playing = v.playing.load (std::memory_order_relaxed);
            c.pending = v.pending.load (std::memory_order_relaxed);
            c.recording = v.recording.load (std::memory_order_relaxed);
            c.recordPending = v.recordPending.load (std::memory_order_relaxed);
            c.stopping = v.stopping.load (std::memory_order_relaxed);
            c.taken = v.taken.load (std::memory_order_relaxed);
            if (c.playing >= 0)
            {
                c.progress = v.progress.load (std::memory_order_relaxed);
                if (const auto left = oneShotSecondsLeft (t, c.playing, c.progress); left >= 0.0)
                    c.oneShotSecond = (int) std::ceil (left);
            }
            if (c.pending >= 0 || c.recordPending >= 0 || c.stopping)
                c.blink = blinkPhaseNow();
            if (liveLook())
            {
                c.monitor = monitorIcon (t, c.armed);
                if (c.playing < 0 && c.recording < 0 && ! c.taken)
                    c.miniStep = (int) std::floor (juce::jmax (0.0, engine.getTransport().positionTicks.load (std::memory_order_relaxed))
                                                   / (double) miniatureStepTicks (engine.getSong()));
            }
            if (c.recording >= 0)
                c.recordTick = mixBits (mixBits (mixBits (1469598103934665603ull, bitsOf (v.recordFrom.load (std::memory_order_relaxed))),
                                                 bitsOf (state->clockNow.load (std::memory_order_relaxed))),
                                        juce::Time::getMillisecondCounter() / 33);
        }
        return c;
    }
    std::vector<ColumnView> columnViews; // [column] what the last tick saw

    // Round 190ld (DL-10, Live 12 Info View: "a miniaturized view of the Arrangement clips if the track is playing back the
    // Arrangement"; filmed in Live 12.4, details2_190ld raw/L01-L05): the status cell of a track that plays its Playlist
    // shows ~20 beats of the Arrangement (55 px in Live: 2.75 px a beat) — its clips as blocks in their colour, the song
    // position as a 1 px #b5b5b5 line that moves right from the start until the middle, then the clips scroll under it.
    // The span and the repaint steps:
    static constexpr int miniatureBeats = 20, miniatureSteps = 80;
    static std::int64_t miniatureStepTicks (const Song&)
    {
        return juce::jmax<std::int64_t> (1, (std::int64_t) miniatureBeats * (std::int64_t) lynx::ppq / miniatureSteps);
    }
    // Round 190ld (DL-11, Live manual 7.3: "A microphone icon appears in an audio track that is set to monitor its input.
    // A keyboard icon appears in a MIDI track"): 1 = mic, 2 = keyboard, 0 = none.
    // Live 12.4 shows it for Monitor "In" only (raw/L09: an armed track on Auto keeps its miniature). LYNX's equivalents:
    // an audio track whose insert monitors "On"; a MIDI track whose channel takes a controller's input whatever is armed
    // (its "Receive from" lock).
    // (per column, worked out again only when the song changed: the grid's 30 Hz tick asks every column)
    int monitorIcon (int t, bool) const
    {
        if (const auto stamp = engine.getSongStamp(); stamp != monitorStamp || monitorCache.empty())
        {
            monitorStamp = stamp;
            monitorCache.assign ((std::size_t) launcher::maxTracks, -1);
        }
        if (t < 0 || t >= (int) monitorCache.size())
            return 0;
        auto& cached = monitorCache[(std::size_t) t];
        if (cached < 0)
            cached = monitorIconNow (t);
        return cached;
    }
    mutable std::vector<int> monitorCache;
    mutable std::uint64_t monitorStamp = 0;
    int monitorIconNow (int t) const
    {
        const auto& song = engine.getSong();
        const auto chain = detailchain::forTrack (song, t);
        if (engine.gridTrackInput (t) == AudioEngine::GridInput::midi)
            return chain.channel >= 0 && chain.channel < (int) song.channels.size() && ! song.channels[(std::size_t) chain.channel].receiveFrom.empty() ? 2 : 0;
        if (chain.insert <= 0 || chain.insert >= (int) song.mixer.size())
            return 0;
        return song.mixer[(std::size_t) chain.insert].monitor == InputMonitor::on ? 1 : 0;
    }

    // round 190im (DL-9): a playing one-shot clip's (Loop off) time left in seconds at the song's tempo; −1: not one
    double oneShotSecondsLeft (int t, int scene, float progress) const
    {
        const auto* grid = gridOf();
        const auto* clip = grid != nullptr ? launcher::clipAt (*grid, t, scene) : nullptr;
        if (clip == nullptr || clip->gridSlot < 0 || clip->gridSlot >= (int) grid->slots.size() || grid->slots[(std::size_t) clip->gridSlot].loop)
            return -1.0;
        const auto bpm = juce::jmax (1.0, engine.getSong().bpm);
        return (1.0 - (double) juce::jlimit (0.0f, 1.0f, progress)) * (double) clip->length / (double) lynx::ppq * 60.0 / bpm;
    }

    static std::uint64_t mixBits (std::uint64_t h, std::uint64_t v) noexcept { return (h ^ v) * 1099511628211ull; }
    template <typename T>
    static std::uint64_t bitsOf (T value) noexcept
    {
        std::uint64_t b = 0;
        std::memcpy (&b, &value, sizeof (value));
        return b;
    }

    // What the whole grid's paint reads that may change without a repaint() of its own: the engine's song (its
    // stamp), the transport, this view's selection, scroll, size and layout (round 190ge).
    std::uint64_t drawnState() const
    {
        std::uint64_t h = 1469598103934665603ull;
        h = mixBits (h, engine.getSongStamp());
        h = mixBits (h, engine.getTransport().playing.load (std::memory_order_relaxed) ? 1u : 0u);
        h = mixBits (h, gridOf() != nullptr ? 1u : 0u);
        for (const auto v : { scrollX, scrollY, selectedTrack, selectedScene, pressedTrack, pressedScene, dropTrack, dropScene, deviceDropTrack,
                              selectedField, getWidth(), getHeight(), sceneWidthNow(), (int) sceneSelected, (int) titleSelected,
                              (int) layoutstyle::current(), mixer != nullptr && mixer->armsShown() ? 1 : 0, columnCount() })
            h = mixBits (h, (std::uint64_t) (std::uint32_t) v);
        return h;
    }

    // One column's own state: its launcher view (playing, waiting, recording, stopping, taken; a playing clip's
    // progress; the recording's start and the clock while it records), whether it is armed, and the blink phase while
    // something in it blinks. Never 0 (0 = not known yet).
    std::uint64_t columnState (int t) const
    {
        std::uint64_t h = 1469598103934665603ull;
        h = mixBits (h, engine.isGridTrackArmed (t) ? 1u : 0u);
        if (const auto* state = engine.launcherState())
        {
            const auto& v = state->tracks[(std::size_t) t];
            const auto playing = v.playing.load (std::memory_order_relaxed), pending = v.pending.load (std::memory_order_relaxed);
            const auto recording = v.recording.load (std::memory_order_relaxed), recordPending = v.recordPending.load (std::memory_order_relaxed);
            const auto stopping = v.stopping.load (std::memory_order_relaxed);
            h = mixBits (h, (std::uint64_t) (std::uint32_t) playing);
            h = mixBits (h, (std::uint64_t) (std::uint32_t) pending);
            h = mixBits (h, (std::uint64_t) (std::uint32_t) recording);
            h = mixBits (h, (std::uint64_t) (std::uint32_t) recordPending);
            h = mixBits (h, (stopping ? 1u : 0u) | (v.taken.load (std::memory_order_relaxed) ? 2u : 0u));
            if (playing >= 0)
                h = mixBits (h, bitsOf (v.progress.load (std::memory_order_relaxed)));
            if (pending >= 0 || recordPending >= 0 || stopping)
                h = mixBits (h, blinkPhaseNow());
            if (recording >= 0) // (the length counter; a MIDI take's name)
            {
                h = mixBits (h, bitsOf (v.recordFrom.load (std::memory_order_relaxed)));
                h = mixBits (h, bitsOf (state->clockNow.load (std::memory_order_relaxed)));
                h = mixBits (h, juce::Time::getMillisecondCounter() / 33);
            }
        }
        return h != 0 ? h : 1;
    }
    std::uint64_t lastDrawnState = 0;
    std::vector<std::uint64_t> columnStates; // [column] its last columnState, 0 = not known yet
    // (Live's selection is light blue while the grid has the keys, grey-blue otherwise: round 190ig, CL-7)
    void focusGained (FocusChangeType) override
    {
        if (liveLook())
            repaint();
    }
    void focusLost (FocusChangeType) override
    {
        if (liveLook())
            repaint();
    }
    void componentMovedOrResized (bool, bool) override {}
    void componentPeerChanged() override { updateTimer(); }
    void componentVisibilityChanged() override { updateTimer(); }
    void updateTimer()
    {
        if (isShowing())
            startTimerHz (30);
        else
            stopTimer();
    }
    using juce::ComponentMovementWatcher::componentVisibilityChanged;
};
} // namespace

std::unique_ptr<juce::Component> makeClipGrid (AudioEngine& engine)
{
    return std::make_unique<ClipGridComponent> (engine);
}

void setClipGridPickListener (juce::Component& grid, std::function<void (int, int, GridPick)> listener)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->onPick = std::move (listener);
}

void setClipGridGroovePoolShown (juce::Component& grid, bool shown)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->showGroovePool (shown);
}

void setClipGridButtonRowShown (juce::Component& grid, bool shown)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->setButtonRow (shown);
}

bool clipGridGroovePoolShown (juce::Component& grid)
{
    auto* g = dynamic_cast<ClipGridComponent*> (&grid);
    return g != nullptr && g->groovePoolShown();
}

void addClipGridMixerViewItems (juce::Component& grid, juce::PopupMenu& menu)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->addMixerViewItems (menu);
}

juce::PopupMenu clipGridSlotMenu (juce::Component& grid, int track, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->buildSlotMenu (track, scene);
    return {};
}

bool clipGridDrag (const juce::var& description, int& track, int& scene)
{
    const auto text = description.toString();
    if (! text.startsWith (juce::String (gridDragDescription) + "|"))
        return false;
    const auto parts = juce::StringArray::fromTokens (text, "|", {});
    if (parts.size() != 3)
        return false;
    track = parts[1].getIntValue();
    scene = parts[2].getIntValue();
    return track >= 0 && scene >= 0;
}

std::vector<int> clipGridSelectedTracks (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->selectedTracks();
    return {};
}

void clipGridSelectTracksForSnapshot (juce::Component& grid, std::vector<int> tracks)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectTracksForSnapshot (std::move (tracks));
}

std::pair<int, int> clipGridSelection (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->selection();
    return { 0, 0 };
}
SessionMixerComponent* clipGridMixer (juce::Component& grid)
{
    auto* g = dynamic_cast<ClipGridComponent*> (&grid);
    return g != nullptr ? g->sessionMixer() : nullptr;
}

void syncClipGridMixerForSnapshot (juce::Component& grid) // (round 190ou: off-screen routes run no timer)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->syncMixerForSnapshot();
}

void setClipGridDeviceDropForSnapshot (juce::Component& grid, int track)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->deviceDropForSnapshot (track);
}
// --- round 190ef ---------------------------------------------------------------------------------------------------------
bool& clipGridClassicLook() { return classicLook; }
bool theme::livelook::active() noexcept { return liveLook(); }

void setClipGridBlinkPhaseForSnapshot (int phase) { blinkPhaseForSnapshot = phase; }

void setClipGridMapStrip (juce::Component& grid, bool on)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->setMapStrip (on);
}

void clipGridRelativeSession (juce::Component& grid, RemoteAction action, int track, bool press)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->relativeSession (action, track, press);
}

void launchClipGridSceneForSnapshot (juce::Component& grid, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->launchSceneForSnapshot (scene);
}

void cancelClipGridSceneLaunch (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->cancelSceneLaunch();
}

bool clipGridSceneLaunchQueued (juce::Component& grid)
{
    auto* g = dynamic_cast<ClipGridComponent*> (&grid);
    return g != nullptr && g->sceneLaunchQueued();
}

void tickClipGridForSnapshot (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->tickForSnapshot();
}

juce::Rectangle<int> clipGridCellForSnapshot (juce::Component& grid, int kind, int track, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->cellForSnapshot (kind, track, scene);
    return {};
}

void clipGridHoverForSnapshot (juce::Component& grid, int kind, int track, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->hoverForSnapshot (kind, track, scene);
}

void clipGridSelectRangeForSnapshot (juce::Component& grid, int track0, int scene0, int track1, int scene1)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectRangeForSnapshot (track0, scene0, track1, scene1);
}

int clipGridSelectedCountForSnapshot (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->rangeCountForCheck();
    return 0;
}

std::pair<int, bool> clipGridSceneSelectionForSnapshot (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->sceneSelectionForSnapshot();
    return { -1, false };
}

void selectClipGridTrack (juce::Component& grid, int track)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectTrackFromArrangement (track);
}

void selectClipGridSlotForSnapshot (juce::Component& grid, int track, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectSlotForSnapshot (track, scene);
}

std::pair<juce::String, juce::String> clipGridHintForSnapshot (juce::Component& grid, juce::Point<int> p)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
    {
        const auto hint = g->hintAt (p);
        return { hint.text, hint.value };
    }
    return {};
}

bool clipGridKeyForSnapshot (juce::Component& grid, const juce::KeyPress& key)
{
    return grid.keyPressed (key);
}

bool& clipGridSceneTempoShown()
{
    return sceneFieldsOn;
}

bool& clipGridSelectNextOnLaunch() { return selectNextOnLaunch; }
bool& clipGridRecordOnSceneLaunch() { return recordOnSceneLaunch; }

void refreshClipGrid (juce::Component& grid)
{
    grid.resized();
    grid.repaint();
}

std::vector<int> clipGridColumnTracks (juce::Component& grid)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->columnTracksForSnapshot();
    return {};
}

void setClipGridSceneFieldsListener (juce::Component& grid, std::function<void()> listener)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->onSceneFieldsChanged = std::move (listener);
}

juce::PopupMenu clipGridSceneMenu (juce::Component& grid, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->buildSceneMenu (scene);
    return {};
}

juce::PopupMenu clipGridHeaderMenu (juce::Component& grid, int track)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->buildHeaderMenu (track);
    return {};
}

juce::PopupMenu clipGridSceneFieldMenu (juce::Component& grid, int scene, int field)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        return g->buildSceneFieldMenu (scene, field);
    return {};
}

void selectClipGridScene (juce::Component& grid, int scene)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectSceneForSnapshot (scene);
}

void selectClipGridSceneField (juce::Component& grid, int scene, int field)
{
    if (auto* g = dynamic_cast<ClipGridComponent*> (&grid))
        g->selectSceneForSnapshot (scene, field);
}

int duplicateClipGridScene (juce::Component& grid, int scene)
{
    auto* g = dynamic_cast<ClipGridComponent*> (&grid);
    return g != nullptr ? g->duplicateSceneForSnapshot (scene) : -1;
}

int captureClipGridScene (juce::Component& grid, int scene, const std::vector<int>& playing)
{
    auto* g = dynamic_cast<ClipGridComponent*> (&grid);
    return g != nullptr ? g->captureSceneForSnapshot (scene, playing) : -1;
}

bool isClipGrid (const juce::Component* component)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<const ClipGridComponent*> (c) != nullptr)
            return true;
    return false;
}

juce::String playlistClipDragDescription (int index)
{
    return juce::String (playlistDragDescription) + "|" + juce::String (index);
}

bool playlistClipDrag (const juce::var& description, int& index)
{
    const auto text = description.toString();
    if (! text.startsWith (juce::String (playlistDragDescription) + "|"))
        return false;
    index = text.fromFirstOccurrenceOf ("|", false, false).getIntValue();
    return index >= 0;
}
} // namespace lynx
