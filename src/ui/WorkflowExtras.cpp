#include "ui/WorkflowExtras.h"

#include <utility>
#include "core/EscClose.h"
#include "core/RulerGestures.h" // round 190nq (#227)
#include "core/Sections.h"      // round 190ol (P1)
#include "core/ChordLane.h"     // round 190pf (P13)

#include "core/TrackClips.h"
#include "core/TrackStates.h"
#include "core/TrackVersions.h" // round 190pe (P20)

#include "core/LaneView.h"
#include "core/LayoutStyle.h"
#include "core/LiveScaleControl.h" // round 190oo (O-5)
#include "core/MidiDrop.h"
#include "core/LoopAnalysis.h" // round 190kx
#include "core/PlaylistViewOptions.h"
#include "core/PlaylistTrackOps.h"
#include "core/TakeCleanup.h"
#include "core/TransportEdit.h"
#include "core/MuteKeys.h"
#include "core/StartPage.h"
#include "core/TrackOnly.h"
#include "core/WheelZones.h"
#include "core/RangeEdit.h" // round 190pa (P14)
#include "engine/AudioEngine.h"
#include "engine/MidiFileIo.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"

namespace lynx::workflowextras
{
namespace
{
AudioEngine* boundEngine = nullptr;

// Track-only mode switched on in a project made with the Channel rack: one step gives each instrument its own track
void offerTracksForEveryInstrument()
{
    if (boundEngine == nullptr || ! trackonly::active())
        return;
    const auto count = trackonly::withoutTrack (boundEngine->getSong());
    if (count == 0)
        return;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Track-only mode"),
                                        tr ("{0} instruments of this project have no track of their own. Give each one an instrument track now? (One step: Edit › Undo takes it back.)",
                                            { juce::String (count) }),
                                        tr ("Yes"), tr ("No"), nullptr, juce::ModalCallbackFunction::create ([] (int result) {
                                            if (result == 1 && boundEngine != nullptr)
                                                boundEngine->editSong ([] (Song& s) { trackonly::assignAll (s); });
                                        }));
}
} // namespace

void bind (AudioEngine* engine) { boundEngine = engine; }

bool channelRackOpened()
{
    // issue #246 (round 190oq): the Channel rack opens in Track-only mode too (FL keeps an instrument track's channel in
    // its Channel rack); a hint says what it lists, once per session
    static bool told = false;
    if (! trackonly::active() || std::exchange (told, true))
        return false;
    ui::postHint ({ tr ("Track-only mode: the Channel rack lists the instruments of the instrument tracks; each plays through its track's mixer track"), {} }, 4000);
    return true;
}

bool trackOnlySwitchedOn()
{
    // (the rack is hidden when the mode comes on, not again at every other switch)
    static bool last = false;
    const auto now = trackonly::active();
    return now && ! std::exchange (last, now);
}

void addLayoutItems (juce::PopupMenu& menu, bool patternLayout)
{
    menu.addSeparator();
    menu.addItem (tr ("Track-only mode (1 track = 1 instrument, the Channel rack hidden)"), patternLayout, trackonly::enabled(),
                  [] { setExtra ("layout.trackOnly", ! trackonly::enabled()); });
}

// --- settings ---------------------------------------------------------------------------------------------------------
bool readUiOption (const juce::String& key, bool on)
{
    if (key == "wheelZones")
        wheelzones::setEnabled (on);
    else if (key == "tempTools")
        temptool::setEnabled (on);
    else if (key == "midiDrop")
        mididrop::setPlaylistDrop (on);
    else if (key == "trackOnly")
        trackonly::setEnabled (on);
    else if (key == "headerWidthDrag") // round 190jq (issue #164)
        headerwidth::draggable = on;
    else if (key == "pointerClickButton") // round 190jq (issue #191): the switch's button, then the switch
        pointerclick::buttonShown = on;
    else if (key == "pointerClick")
        pointerclick::on = on && pointerclick::buttonShown;
    else if (key == "clipLanesPattern") // round 190jq (issue #191.3)
        cliplanes::patternOn = on;
    else if (key == "clipLanesLive")
        cliplanes::liveOn = on;
    else if (key == "wToStart") // issue #195: REAPER's W goes to the start (LYNX extra)
        transportedit::setWGoesToStart (on);
    else if (key == "simpleLoopDetect") // round 190kx: Simple drops take the tempo and key found in the audio
        loopanalysis::dropDetects() = on;
    else if (key == "escClosesWindows") // issue #217 (round 190ng): FL's Esc closes the focused window
        escclose::enabled() = on;
    else if (key == "trackClipsShown") // round 190ne: track clips listed in the pattern selector too (the Picker group's switch)
        trackclips::setShownInSelector (on);
    else if (key == "rulerSeeksAnyTool") // issue #227 (round 190nq, LYNX extra)
        ruler::seekWithAnyTool() = on;
    else if (key == "sectionsLane") // round 190ol (P1): the Pattern layout's Sections lane
        sections::patternLaneOn() = on;
    else if (key == "chordLane") // round 190pf (P13): the Chord lane, every layout
        chordlane::laneOn() = on;
    else if (key == "prChordHighlight") // round 190pf (P13): the Piano roll lights the Chord lane's chord
        chordlane::pianoRollHighlight() = on;
    else if (key == "clipLanesFoldedLive") // round 190oo (CS O-3, LYNX extra)
        cliplanes::foldedOnOpen = on;
    else if (key == "liveScaleControl") // round 190oo (O-5, LYNX extra)
        livescale::shown = on;
    else if (key == "trackStates") // round 190op (P4, LYNX extra): Hide / Deactivate in the Pattern layout's menus
        trackstates::patternMenus() = on;
    else if (key == "trackVersions") // round 190pe (P20, LYNX extra): Track versions in the Pattern layout's track menu
        trackversions::patternMenus() = on;
    else if (key == "muteKeyToggles") // issue #249 (round 190oq, LYNX extra)
        mutekeys::setToggles (on);
    else if (key == "startPage") // issue #252 (round 190oq; on by default, FL-exact)
        startpage::setEnabled (on);
    else if (key == "rangeEdits") // round 190pa (P14, LYNX extra): Ctrl+Alt+drag ranges in the Pattern layout
        rangeedit::patternLayoutOn() = on;
    else
        return false;
    return true;
}

bool readUiValue (const juce::String& key, const juce::String& value)
{
    if (key != "headerWidthExtra") // round 190jq (issue #164): px added to the header column's own width
        return false;
    headerwidth::extra = headerwidth::clampExtra (value.getIntValue());
    return true;
}

juce::String uiOptionsText()
{
    return "\nwheelZones=" + juce::String (wheelzones::enabled() ? 1 : 0) + "\ntempTools=" + juce::String (temptool::enabled() ? 1 : 0)
           + "\nmidiDrop=" + juce::String (mididrop::playlistDrop() ? 1 : 0) + "\ntrackOnly=" + juce::String (trackonly::enabled() ? 1 : 0)
           // round 190jq (the button before its switch: the loader reads them in this order)
           + "\nheaderWidthDrag=" + juce::String (headerwidth::draggable ? 1 : 0) + "\nheaderWidthExtra=" + juce::String (headerwidth::clampExtra (headerwidth::extra))
           + "\npointerClickButton=" + juce::String (pointerclick::buttonShown ? 1 : 0) + "\npointerClick=" + juce::String (pointerclick::on ? 1 : 0)
           + "\nclipLanesPattern=" + juce::String (cliplanes::patternOn ? 1 : 0) + "\nclipLanesLive=" + juce::String (cliplanes::liveOn ? 1 : 0)
           + "\nwToStart=" + juce::String (transportedit::wGoesToStart() ? 1 : 0) // (round 190jz, issue #195)
           + "\nsimpleLoopDetect=" + juce::String (loopanalysis::dropDetects().load() ? 1 : 0) // (round 190kx)
           + "\nescClosesWindows=" + juce::String (escclose::enabled() ? 1 : 0) // (round 190ng, issue #217)
           + "\ntrackClipsShown=" + juce::String (trackclips::shownInSelector() ? 1 : 0) // (round 190ne)
           + "\nrulerSeeksAnyTool=" + juce::String (ruler::seekWithAnyTool() ? 1 : 0) // (round 190nq, issue #227)
           + "\nsectionsLane=" + juce::String (sections::patternLaneOn() ? 1 : 0) // (round 190ol, P1)
           + "\nchordLane=" + juce::String (chordlane::laneOn() ? 1 : 0) // (round 190pf, P13)
           + "\nprChordHighlight=" + juce::String (chordlane::pianoRollHighlight() ? 1 : 0) // (round 190pf, P13)
           + "\nclipLanesFoldedLive=" + juce::String (cliplanes::foldedOnOpen ? 1 : 0) // (round 190oo, CS O-3)
           + "\nliveScaleControl=" + juce::String (livescale::shown ? 1 : 0) // (round 190oo, O-5)
           + "\ntrackStates=" + juce::String (trackstates::patternMenus() ? 1 : 0) // (round 190op, P4)
           + "\ntrackVersions=" + juce::String (trackversions::patternMenus() ? 1 : 0) // (round 190pe, P20)
           + "\nmuteKeyToggles=" + juce::String (mutekeys::toggles() ? 1 : 0) // (round 190oq, issue #249)
           + "\nstartPage=" + juce::String (startpage::enabled() ? 1 : 0) // (round 190oq, issue #252)
           + "\nrangeEdits=" + juce::String (rangeedit::patternLayoutOn() ? 1 : 0); // (round 190pa, P14)
}

std::optional<bool> extraOn (std::string_view id)
{
    if (id == "playlist.wheelZones")
        return wheelzones::enabled();
    if (id == "editing.tempTools")
        return temptool::enabled();
    if (id == "playlist.midiDrop")
        return mididrop::playlistDrop();
    if (id == "layout.trackOnly")
        return trackonly::enabled();
    if (id == "playlist.headerWidth") // round 190jq
        return headerwidth::draggable;
    if (id == "playlist.pointerClick")
        return pointerclick::buttonShown;
    if (id == "playlist.clipLanes") // (the Pattern layout's switch; the Clip-launch layout has its own, on by default)
        return cliplanes::patternOn;
    if (id == "transport.wToStart")
        return transportedit::wGoesToStart();
    if (id == "simple.loopDetect") // round 190kx
        return loopanalysis::dropDetects().load();
    if (id == "general.escCloses") // round 190ng (issue #217)
        return escclose::enabled();
    if (id == "playlist.rulerSeeksAnyTool") // round 190nq (issue #227)
        return ruler::seekWithAnyTool();
    if (id == "playlist.sectionsLane") // round 190ol (P1)
        return sections::patternLaneOn();
    if (id == "playlist.chordLane") // round 190pf (P13)
        return chordlane::laneOn();
    if (id == "pianoRoll.chordHighlight") // round 190pf (P13)
        return chordlane::pianoRollHighlight();
    if (id == "playlist.clipLanesFolded") // round 190oo (CS O-3)
        return cliplanes::foldedOnOpen;
    if (id == "controlBar.liveScale") // round 190oo (O-5)
        return livescale::shown;
    if (id == "playlist.trackStates") // round 190op (P4)
        return trackstates::patternMenus();
    if (id == "playlist.trackVersions") // round 190pe (P20)
        return trackversions::patternMenus();
    if (id == "editing.muteKeyToggles") // round 190oq (issue #249)
        return mutekeys::toggles();
    if (id == "general.startPage") // round 190oq (issue #252)
        return startpage::enabled();
    if (id == "playlist.rangeEdits") // round 190pa (P14)
        return rangeedit::patternLayoutOn();
    return std::nullopt;
}

bool setExtra (std::string_view id, bool on)
{
    if (id == "playlist.wheelZones")
        wheelzones::setEnabled (on);
    else if (id == "editing.tempTools")
    {
        temptool::setEnabled (on);
        if (! on)
            tempTools().toolKey (temptool::Editor::none, 0, EditTool::pencil, EditTool::pencil, nullptr, nullptr); // (ends a hold)
    }
    else if (id == "playlist.midiDrop")
        mididrop::setPlaylistDrop (on);
    else if (id == "layout.trackOnly")
        trackonly::setEnabled (on);
    else if (id == "playlist.headerWidth") // round 190jq (issue #164; the dragged width is kept for when it is on again)
        headerwidth::draggable = on;
    else if (id == "playlist.pointerClick") // (issue #191: the toolbar switch, lit when shown)
        pointerclick::setButtonShown (on);
    else if (id == "playlist.clipLanes") // (issue #191.3)
        cliplanes::patternOn = on;
    else if (id == "transport.wToStart")
        transportedit::setWGoesToStart (on);
    else if (id == "simple.loopDetect") // round 190kx
        loopanalysis::dropDetects() = on;
    else if (id == "general.escCloses") // round 190ng (issue #217)
        escclose::enabled() = on;
    else if (id == "playlist.rulerSeeksAnyTool") // round 190nq (issue #227)
        ruler::seekWithAnyTool() = on;
    else if (id == "playlist.sectionsLane") // round 190ol (P1)
        sections::patternLaneOn() = on;
    else if (id == "playlist.chordLane") // round 190pf (P13)
        chordlane::laneOn() = on;
    else if (id == "pianoRoll.chordHighlight") // round 190pf (P13)
        chordlane::pianoRollHighlight() = on;
    else if (id == "playlist.clipLanesFolded") // round 190oo (CS O-3: from the next project opened)
        cliplanes::foldedOnOpen = on;
    else if (id == "controlBar.liveScale") // round 190oo (O-5)
        livescale::shown = on;
    else if (id == "playlist.trackStates") // round 190op (P4)
        trackstates::patternMenus() = on;
    else if (id == "playlist.trackVersions") // round 190pe (P20)
        trackversions::patternMenus() = on;
    else if (id == "editing.muteKeyToggles") // round 190oq (issue #249)
        mutekeys::setToggles (on);
    else if (id == "general.startPage") // round 190oq (issue #252)
        startpage::setEnabled (on);
    else if (id == "playlist.rangeEdits") // round 190pa (P14)
        rangeedit::patternLayoutOn() = on;
    else
        return false;
    if (onChanged)
        onChanged();
    if (id == "layout.trackOnly" && on)
        offerTracksForEveryInstrument();
    return true;
}

bool runAction (const std::string& id)
{
    if (id == "playlist.wheelZones" || id == "editing.tempTools" || id == "playlist.midiDrop" || id == "layout.trackOnly" || id == "playlist.headerWidth"
        || id == "playlist.clipLanesFolded" || id == "controlBar.liveScale") // (round 190oo: CS O-3, O-5)
        return setExtra (id, ! extraOn (id).value_or (true));
    // round 190jq: the pointer-click switch itself (toolbar button, menu, key); on also shows its button
    if (id == "playlist.pointerClick")
    {
        pointerclick::setOn (! pointerclick::on);
        ui::postHint ({ tr ("Click on an empty spot selects and moves the song position (pointer click)"), pointerclick::on ? tr ("On") : tr ("Off"), false, false });
        if (onChanged)
            onChanged();
        return true;
    }
    // automation clips as lanes: the switch of the layout shown (Pattern / Clip-launch)
    if (id == "playlist.clipLanes")
    {
        const auto clipLaunch = layoutstyle::current() == layoutstyle::Style::clipLaunch;
        cliplanes::setShownIn (clipLaunch, ! cliplanes::shownIn (clipLaunch, false));
        if (onChanged)
            onChanged();
        return true;
    }
    if (id == "playlist.clipsToLanes") // #191.3
    {
        if (boundEngine == nullptr || ! autolanes::shown() || laneview::movableClips (boundEngine->getSong()).empty())
            return true;
        int moved = 0;
        boundEngine->editSong ([&moved] (Song& s) { moved = laneview::clipsToLanes (s); });
        ui::postHint ({ tr ("{0} automation clips moved into their tracks' lanes (Edit › Undo takes it back)", { juce::String (moved) }), {} }, 3000);
        if (onChanged)
            onChanged();
        return true;
    }
    return false;
}

void addPlaylistMenuItems (const std::function<void (const char*, const juce::String&, bool, bool)>& add)
{
    // (#191.3: shown while the lanes are on, with how many clips it would move)
    if (autolanes::shown())
    {
        const auto movable = boundEngine != nullptr ? (int) laneview::movableClips (boundEngine->getSong()).size() : 0;
        add ("playlist.clipsToLanes", tr ("Move automation clips into their tracks' lanes ({0})", { juce::String (movable) }), movable > 0, false);
    }
    // round 190jq (issues #191.3, #191, #164)
    {
        const auto style = layoutstyle::current();
        add ("playlist.clipLanes", tr ("Automation clips as lanes under their tracks"), style != layoutstyle::Style::simple,
             cliplanes::shownIn (style == layoutstyle::Style::clipLaunch, style == layoutstyle::Style::simple));
        if (style == layoutstyle::Style::clipLaunch) // round 190oo (CS O-3, LYNX extra: Live folds its automation lanes)
            add ("playlist.clipLanesFolded", tr ("Automation lanes start folded when a project opens (Clip-launch layout)"), true, cliplanes::foldedOnOpen);
        add ("playlist.pointerClick", tr ("Click on an empty spot selects and moves the song position (pointer click)"), style != layoutstyle::Style::simple, pointerclick::on);
        add ("playlist.headerWidth", tr ("Drag the track header column's right edge to widen it"), style != layoutstyle::Style::simple, headerwidth::draggable);
    }
    add ("playlist.wheelZones", tr ("Wheel: scroll over the track headers, zoom over the arrangement"), true, wheelzones::enabled());
    add ("editing.tempTools", tr ("Hold a tool key for a temporary tool"), true, temptool::enabled());
    add ("playlist.midiDrop", tr ("MIDI files dropped on the Playlist become pattern clips"), true, mididrop::playlistDrop());
}

// --- #191.1 temporary tools -------------------------------------------------------------------------------------------
TempToolHold& tempTools()
{
    static TempToolHold instance;
    return instance;
}

void TempToolHold::toolKey (temptool::Editor editor, int keyCode, EditTool before, EditTool chosen, std::function<EditTool()> now,
                            std::function<void (EditTool)> back)
{
    if (keyCode == 0 || ! temptool::enabled())
    {
        hold.cancel(); // a tool picked by a menu or a click ends any hold: that choice stays
        stopTimer();
        toolNow = nullptr;
        restore = nullptr;
        return;
    }
    if (hold.press (editor, keyCode, before, chosen, juce::Time::getMillisecondCounter()))
    {
        toolNow = std::move (now);
        restore = std::move (back);
        startTimerHz (30); // (only while a key is held)
    }
}

void TempToolHold::releaseNow (std::uint32_t now)
{
    stopTimer();
    EditTool back = EditTool::pencil;
    const auto current = toolNow ? toolNow() : hold.held;
    if (hold.release (now, current, back) && restore)
        restore (back);
    toolNow = nullptr;
    restore = nullptr;
}

void TempToolHold::timerCallback()
{
    if (! hold.active())
    {
        stopTimer();
        return;
    }
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        hold.mouseUsed();
    if (! juce::KeyPress::isKeyCurrentlyDown (hold.keyCode))
        releaseNow (juce::Time::getMillisecondCounter());
}

// --- #191.2 MIDI files ------------------------------------------------------------------------------------------------
bool isMidiFile (const juce::String& path)
{
    const juce::File f (path);
    return f.hasFileExtension ("mid;midi;smf") && ! f.isDirectory();
}

bool anyMidiFile (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return isMidiFile (f); });
}

namespace
{
juce::String refusal (const std::string& error, const juce::File& file)
{
    if (error == "audio track")
        return tr ("{0}: an audio track can't hold MIDI. Drop it on an instrument track or an empty track", { file.getFileName() });
    if (error == "no notes")
        return tr ("{0} has no notes", { file.getFileName() });
    return tr ("{0}: no track free for it", { file.getFileName() });
}

// `apply` makes the edit; `placed` hears each real (not trial) result, to move on to the next place
template <typename Apply, typename Placed>
bool dropEach (AudioEngine& engine, const juce::StringArray& files, Apply&& apply, Placed&& placed)
{
    bool any = false;
    for (const auto& path : files)
    {
        if (! isMidiFile (path))
            continue;
        const juce::File file (path);
        juce::String error;
        const auto midi = MidiFileIo::parseFile (file, error);
        if (! midi)
        {
            ui::postNotice ({ tr ("{0} could not be read: {1}", { file.getFileName(), error }), {} }, 4000);
            continue;
        }
        const auto name = file.getFileNameWithoutExtension().toStdString();
        // (a refused drop leaves no undo step: tried on a copy first)
        auto trial = engine.getSong();
        if (const auto check = apply (trial, *midi, name); ! check.ok)
        {
            ui::postNotice ({ refusal (check.error, file), {} }, 4000);
            continue;
        }
        mididrop::Result r;
        engine.editSong ([&] (Song& s) { r = apply (s, *midi, name); });
        any = any || r.ok;
        if (r.ok)
            placed (r);
        if (r.ok)
            ui::postHint ({ tr ("MIDI file {0}: a clip of pattern \"{1}\"", { file.getFileName(), juce::String::fromUTF8 (name.c_str()) }), {} }, 2500);
    }
    return any;
}
} // namespace

bool dropMidiOnPlaylist (AudioEngine& engine, const juce::StringArray& files, int track, std::int64_t start, bool liveStyle)
{
    liveStyle = liveStyle || trackonly::active(); // (Track-only mode: one instrument per track, as Live's arrangement)
    int next = track;
    return dropEach (
        engine, files,
        [&next, start, liveStyle] (Song& s, const midiimport::ParsedMidi& midi, const std::string& name) {
            return mididrop::dropOnPlaylist (s, midi, name, next, start, liveStyle);
        },
        [&next] (const mididrop::Result& r) {
            if (! r.tracks.empty())
                next = *std::max_element (r.tracks.begin(), r.tracks.end()) + 1; // (several files: one below the other)
        });
}

bool dropMidiOnSlot (AudioEngine& engine, const juce::StringArray& files, int track, int scene)
{
    int next = scene;
    return dropEach (
        engine, files,
        [&next, track] (Song& s, const midiimport::ParsedMidi& midi, const std::string& name) { return mididrop::dropOnSlot (s, midi, name, track, next); },
        [&next] (const mididrop::Result&) { ++next; });
}

// --- #191.6 takes and recorded envelopes -----------------------------------------------------------------------------
void showPickerItemMenu (AudioEngine& engine, ClipKind kind, int index, std::function<void()> edited)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    if (kind == ClipKind::audio && index >= 0 && index < (int) song.channels.size())
    {
        const auto name = juce::String::fromUTF8 (song.channels[(std::size_t) index].name.c_str());
        menu.addSectionHeader (name);
        menu.addItem (tr ("Delete (its clips and its channel)"), [&engine, index, edited] {
            engine.editSong ([index] (Song& s) { takecleanup::deleteAudioClip (s, index); });
            if (edited)
                edited();
        });
    }
    else if (kind == ClipKind::automation && index >= 0 && index < (int) song.automations.size())
    {
        const auto name = juce::String::fromUTF8 (song.automations[(std::size_t) index].name.c_str());
        menu.addSectionHeader (name);
        const auto uses = takecleanup::automationUses (song, index);
        menu.addItem (uses > 0 ? tr ("Delete (with its {0} clips)", { juce::String (uses) }) : tr ("Delete"), [&engine, index, edited] {
            engine.editSong ([index] (Song& s) { takecleanup::deleteAutomation (s, index); });
            if (edited)
                edited();
        });
    }
    else
        return;
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void addPickerCleanupItems (juce::PopupMenu& menu, AudioEngine& engine, std::function<void()> edited)
{
    const auto& song = engine.getSong();
    const auto audio = (int) pltracks::unusedAudioClipChannels (song).size();
    const auto automations = (int) takecleanup::unusedAutomations (song).size();
    menu.addItem (tr ("Purge unused audio clips") + " (" + juce::String (audio) + ")", audio > 0, false, [&engine, edited] {
        engine.editSong ([] (Song& s) { pltracks::purgeUnusedAudioClips (s); });
        if (edited)
            edited();
    });
    menu.addItem (tr ("Delete unused automation clips ({0})", { juce::String (automations) }), automations > 0, false, [&engine, edited] {
        engine.editSong ([] (Song& s) { takecleanup::deleteUnusedAutomations (s); });
        if (edited)
            edited();
    });
}

void addBrowserDeleteItem (juce::PopupMenu& menu, AudioEngine& engine, bool automation, int index)
{
    const auto& song = engine.getSong();
    if (automation)
    {
        if (index < 0 || index >= (int) song.automations.size())
            return;
        menu.addSeparator();
        menu.addItem (tr ("Delete"), [&engine, index] { engine.editSong ([index] (Song& s) { takecleanup::deleteAutomation (s, index); }); });
        return;
    }
    if (index < 0 || index >= (int) song.channels.size() || song.channels[(std::size_t) index].instrument != InstrumentKind::audioClip)
        return;
    menu.addSeparator();
    menu.addItem (tr ("Delete (its clips and its channel)"), [&engine, index] {
        engine.editSong ([index] (Song& s) { takecleanup::deleteAudioClip (s, index); });
    });
}
} // namespace lynx::workflowextras
