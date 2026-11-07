#include "ui/VideoTracks.h"
#include "ui/AdrWindow.h"
#include "ui/ThemeSettings.h"
#include "ui/ColourPicker.h"
#include "core/ClipOps.h"
#include "core/NoteNames.h"
#include "ui/FileChoosers.h"
#include "ui/ProjectLoading.h"
#include "ui/UiTrace.h"
#include "core/LoadTrace.h"
#include "ui/MainComponent.h"
#include "core/Performance.h"

#include "core/TransportEdit.h"
#include "ui/CrashReporter.h"
#include "ui/DanceView.h"
#include "engine/AudioLogger.h"
#include "ui/PluginSkin.h"
#include "platform/Paths.h"
#include "ui/SetupWizard.h"
#include "core/FlPluginDb.h"
#include "core/Preferences.h"

#include "core/EditorPolish.h"

#include "ui/HistoryPanel.h"
#include "ui/MissingFilesDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/PluginManager.h"
#include "ui/ShortcutManager.h"
#include "ui/WaveformView.h"
#include "ui/QuickAddPalette.h"
#include "ui/PluginPickerView.h"
#include "ui/PatcherEditor.h"
#include "ui/ToolWindow.h"
#include "ui/LoopStarterWindow.h"
#include "ui/WindowOwner.h"

#include "engine/PluginSandbox.h"
#include "platform/FileTypes.h"
#include "platform/CrashGuard.h"
#include "platform/LinuxRealtime.h"
#include "platform/Package.h"

#include "engine/Text.h"

#include "core/MetronomeVoice.h"
#include "core/RackOps.h"
#include "core/Templates.h"
#include "ui/NewProjectDialog.h"
#include "ui/PluginMonitorWindow.h"
#include "ui/TouchKeyboard.h"
#include "engine/MidiFileIo.h"
#include "engine/ProjectFile.h"
#include "engine/ProjectPackage.h"
#include "engine/SessionPack.h"
#include "ui/SessionDialogs.h"
#include "engine/LiveSessions.h"
#include "ui/LiveChat.h"
#include "core/ProjectSafety.h"
#include "core/MissingFiles.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/Dialogs.h"
#include "ui/EventEditor.h"
#include "ui/EditorTools.h"
#include "ui/AudioEditor.h"
#include "ui/TempoTapper.h"
#include "ui/ExportDialog.h"
#include "ui/AlsExportDialog.h"
#include "ui/FlStudioExportDialog.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginWindow.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

#include "core/HintMarkup.h"
#include "core/SongStats.h"

namespace lynx
{
namespace
{
using workspace::Panel;

constexpr Panel panelOrder[] = { Panel::playlist, Panel::channelRack, Panel::pianoRoll, Panel::mixer, Panel::browser };

const char* panelAction (Panel p)
{
    switch (p)
    {
        case Panel::playlist: return "panel.playlist";
        case Panel::channelRack: return "panel.channelRack";
        case Panel::pianoRoll: return "panel.pianoRoll";
        case Panel::mixer: return "panel.mixer";
        case Panel::browser: return "panel.browser";
    }
    return "";
}

juce::String panelName (Panel p)
{
    switch (p)
    {
        case Panel::playlist: return "Playlist";
        case Panel::channelRack: return "Channel Rack";
        case Panel::pianoRoll: return "Piano Roll";
        case Panel::mixer: return "Mixer";
        case Panel::browser: return "Browser";
    }
    return {};
}
} // namespace

MainComponent::MainComponent (AudioEngine& e, bool restoreUserSettings)
    : persistent (restoreUserSettings),
      engine (e),
      rack (e),
      pianoRoll (e),
      playlist (e),
      mixer (e),
      browser (e)
{
    heldKeys.setSender ([this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on, true); });
    connectBrowser (browser);
    addChildComponent (updateBar);
    updateBar.onVisibilityChanged = [this] { resized(); };
    if (restoreUserSettings) // (the real window: Update saves the project as on quit and reopens it)
        updater::setHost ({ [safe = juce::Component::SafePointer<MainComponent> (this)] (std::function<void()> then) {
                               if (safe != nullptr)
                                   safe->confirmDiscard (std::move (then));
                           },
                            [safe = juce::Component::SafePointer<MainComponent> (this)] {
                                return safe != nullptr ? safe->currentFile : juce::File();
                            } });
    mixeroptions::changed = [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
        {
            safe->saveUiSettings();
            safe->playlist.repaint();
        }
    };
    mixeroptions::setShowUsedOnly = [safe = juce::Component::SafePointer<MainComponent> (this)] (bool on) {
        if (safe != nullptr)
            safe->mixer.getStripsForSnapshot().setShowUsedOnly (on);
    };
    mixeroptions::renderTracks = [safe = juce::Component::SafePointer<MainComponent> (this)] (std::vector<int> tracks,
                                                                                             std::function<void (const std::vector<juce::File>&)> done) {
        if (safe != nullptr)
            showExportDialogForTracks (safe->engine, safe->currentFile.getParentDirectory(), safe->projectName, tracks, std::move (done));
    };
    if (persistent)
    {
        engine.getRollingLog().setEnabled (true); // on by default, as in FL (Tools › Audio logger)
        loadUiSettings();
        // the start-up song follows Options › Mixer too (round 163) and the automatic crossfades of new projects (166e)
        engine.setSong (makeNewProject());
        skin::onStyleSettingsChanged = [safe = juce::Component::SafePointer<MainComponent> (this)] { // an editor's own style was picked
            if (safe != nullptr) safe->saveUiSettings();
        };
        loadKeyMap();
        startSession();
        if (BrowserComponent::sampleBrowserWasOpen()) // it was open when LYNX closed (round 163)
            juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<MainComponent> (this)] {
                if (safe != nullptr && findToolWindow ("sampleBrowser") == nullptr)
                    safe->openSampleBrowser();
            });
        EditorGlobals::get().onOptionsChanged = [safe = juce::Component::SafePointer<MainComponent> (this)] { // Follow on/off
            if (safe != nullptr) safe->saveUiSettings();
        };
    }
    PianoRollComponent::shortcutText = [this] (const std::string& id) { return shortcutText (id.c_str()); };
    pianoRoll.onNoteNamesChanged = [this] { saveUiSettings(); };
    // FL's EEPlayBtnClick (FLEngine 0xe4bd40, REA round 169f): Pattern mode first (also while the song plays), then the
    // play / pause of the transport. EEPlayBtnMouseDown (0xe4bda0), right button: FL's Stop (FUN_011ca520 (0, 0), the very
    // call of the toolbar's StopBtnClick), which in FL goes back to where playback started, pressed again to the start
    // (measured in FL 2026, round 169f, Pattern mode after a ruler click)
    pianoRoll.onPlayPause = [this] {
        if (engine.getSong().mode != PlayMode::pattern)
            toggleMode();
        playPause();
    };
    pianoRoll.onPlayRightClick = [this] {
        const auto playing = engine.getTransport().playing.load() || engine.isWaitingForInput() || engine.isCountingIn();
        const auto target = transportedit::rollPlayRightClickTarget (playing, engine.getTransport().positionTicks.load(), playStartTicks);
        engine.stop();
        if (target > 0)
            engine.seek (target);
        else
            engine.rewind();
        playStartTicks = target;
    };
    pianoRoll.onBrowseScores = [this] { // (round 168g: the Piano roll's File › Browse scores)
        showPanel (Panel::browser);
        browser.showScores();
    };

    EventEditorHooks::onBrowseAutomations = [this] { // (the Event editor's File › Browse automations: FL's Scores\\Automation)
        showPanel (Panel::browser);
        browser.showScores ("Automation");
    };

    newButton.setTooltip (tr ("New project") + " (Ctrl+N)");
    newButton.onClick = [this] { newProject(); };
    openButton.setTooltip (tr ("Open project") + " (Ctrl+O)");
    openButton.onClick = [this] { chooseFileToOpen(); };
    saveButton.setTooltip (tr ("Save project") + " (Ctrl+S)");
    saveButton.onClick = [this] { save (false); };
    exportButton.setTooltip (tr ("Export audio: WAV, FLAC or OGG") + " (Ctrl+R)");
    exportButton.onClick = [this] { exportAudio(); };
    auto withKey = [this] (const juce::String& text, const char* id) {
        const auto key = shortcutText (id).trim();
        return key.isEmpty() ? text : text + " (" + key + ")";
    };
    undoButton.setTooltip (withKey (tr ("Undo"), "edit.undo"));
    undoButton.onClick = [this] { flUndo(); }; // FL's toolbar Undo is its Edit › Undo (UndoAction)
    undoButton.onRightClick = [this] { showHistory(); };
    redoButton.setTooltip (withKey (tr ("Redo"), "edit.redo"));
    redoButton.onClick = [this] { undoRedo (true); };
    redoButton.onRightClick = [this] { showHistory(); };
    // FL's toolbar picker button (round 171c): a click opens the Plugin picker (F8); a right-click LYNX's quick-add palette
    pluginPickerButton.setTooltip (withKey (tr ("Plugin picker: every plugin as a picture; drag one onto the Channel Rack or the Mixer (right-click: quick-add palette)"),
                                            "view.pluginPicker"));
    pluginPickerButton.onClick = [this] { togglePluginPicker(); };
    pluginPickerButton.onRightClick = [this] { showQuickAdd(); };

    // transport: Play toggles play / stop-and-back; Stop returns to the start; double-click Stop = all notes off
    playButton.setTooltip (tr ("Play; pressed again, stops and goes back to where it started") + " (Space; Ctrl+Space pauses)");
    playButton.onClick = [this] { playStop(); };
    stopButton.setTooltip (tr ("Stop and return to the start (double-click: back to the start and all notes off)"));
    stopButton.onClick = [this] { stopAndRewind(); };
    stopButton.onDoubleClick = [this] { // FL-like: a double Stop goes back to the very start (and silences hanging notes)
        engine.panic();
        stopAndRewind (true);
    };
    stopButton.onRightClick = [this] { showStopMenu(); };
    output.onModeChanged = [this] { saveUiSettings(); };
    recordButton.setOnColour (theme::colour::record);
    recordButton.setIconColour (theme::colour::record.withMultipliedSaturation (0.8f));
    recordButton.onClick = [this] { toggleRecording(); };
    recordMenuButton.setTooltip (tr ("Recording options: what to record"));
    recordMenuButton.onClick = [this] { showRecordMenu(); };
    recordButton.onRightClick = [this] { showRecordMenu(); };

    // Pattern / Song: a two-segment switch (L toggles); right-click shows the matching panel
    // as in FL, a two-line switch in front of the transport buttons: pattern on top, song below
    patternModeButton.setText (tr ("Pattern"));
    songModeButton.setText (tr ("Song"));
    for (auto* b : { &patternModeButton, &songModeButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::textPrimary);
    }
    patternModeButton.setTooltip (tr ("Pattern mode: plays the current pattern in a loop") + " (L)\n" + tr ("Right-click: show the Channel Rack"));
    songModeButton.setTooltip (tr ("Song mode: plays the Playlist arrangement") + " (L)\n" + tr ("Right-click: show the Playlist"));
    patternModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::pattern)
            toggleMode();
    };
    songModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::song)
            toggleMode();
    };
    patternModeButton.onRightClick = [this] { showPanel (Panel::channelRack); };
    songModeButton.onRightClick = [this] { showPanel (Panel::playlist); };

    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setOnColour (theme::colour::bg6);
    metronomeButton.setIconColourWhenOn (theme::colour::accent);
    metronomeButton.setTooltip (tr ("Metronome"));
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };
    midiLight.onReconnect = [this] {
        engine.reconnectMidiInputs();
        midiInputTicks = 0; // shows the reopened inputs at the next tick
    };
    metronomeButton.onRightClick = [this] { showMetronomeMenu(); };

    typingPianoButton.setOnColour (theme::colour::bg6);
    typingPianoButton.setIconColourWhenOn (theme::colour::accent);
    typingPianoButton.onClick = [this] { toggleTypingPiano(); };
    typingPianoButton.onRightClick = [this] { showTypingPianoMenu(); };

    // count-in and wait-for-input switches next to the metronome, as in FL's recording panel
    for (auto* b : { &countInButton, &waitInputButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::accent);
    }
    countInButton.onClick = [this] { runAction ("record.countIn"); };
    waitInputButton.onClick = [this] { runAction ("record.waitForInput"); };
    // round 170e2: FL's "Blend recorded notes" switch (on = overdub, off = replace) and its tempo tapper button
    blendButton.setOnColour (theme::colour::bg6);
    blendButton.setIconColourWhenOn (theme::colour::accent);
    blendButton.onClick = [this] { runAction ("record.blend"); };
    tapTempoButton.onClick = [this] { openTempoTapper (engine); };
    // round 170e3: loop record, step edit and punch in/out as switches beside them
    for (auto* b : { &loopRecordButton, &stepEditButton, &punchButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::accent);
    }
    loopRecordButton.onClick = [this] { runAction ("record.loop"); };
    stepEditButton.onClick = [this] { runAction ("record.stepEdit"); };
    punchButton.onClick = [this] { runAction ("record.punch"); };

    for (int i = 0; i < workspace::panelCount; ++i)
    {
        const auto panel = (Panel) i;
        auto& b = panelButtons[(std::size_t) i];
        b.setFlat (true);
        b.setOnColour (theme::colour::bg5);
        b.setIconColourWhenOn (theme::colour::textPrimary);
        b.setTooltip (panelName (panel) + " (" + keys::displayString (keyMap.chordsFor (panelAction (panel)).empty() ? keys::Chord {}
                                                                                                          : keyMap.chordsFor (panelAction (panel)).front()) + ")\n"
                      + tr ("Click: show, focus or hide.  Right-click: show maximized"));
        b.onClick = [this, panel] { togglePanel (panel); };
        b.onRightClick = [this, panel] {
            showPanel (panel);
            if (! workspace.isMaximised (panel) && ! workspace.isDetached (panel))
                workspace.toggleMaximised (panel);
            refreshPanelButtons();
        };
    }

    patternBox.setTooltip (tr ("Current pattern (Numpad +/-: next/previous, F4: next empty, F2: rename)"));
    patternBox.onStep = [this] (int delta) { stepPatternFromSelector (delta); };
    gridBox.setScrollWheelEnabled (true);
    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedItemIndex();

        if (index >= 0 && index != engine.getSong().currentPattern)
            selectPattern (index);
    };

    newPatternButton.setTooltip (tr ("New pattern"));
    newPatternButton.onClick = [this]
    {
        engine.editSong ([] (Song& s) {
            Pattern p;
            p.name = newPatternName (s);
            s.patterns.push_back (std::move (p));
            s.currentPattern = (int) s.patterns.size() - 1;
        });
    };

    swingSlider.setRange (0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setValue (engine.getSong().swing, juce::dontSendNotification);
    swingSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    swingSlider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; };
    swingSlider.onValueChange = [this] { engine.editSong ([v = (float) swingSlider.getValue()] (Song& s) { s.swing = v; }); };
    swingSlider.setTooltip (tr ("Swing of the Channel Rack (all channels)"));
    swingSlider.setMouseClickGrabsKeyboardFocus (false);
    swingSlider.setWantsKeyboardFocus (false);

    patternMenuButton.setTooltip (tr ("Pattern: rename, color, clone, insert, split, length, delete"));
    patternMenuButton.onClick = [this] { showPatternMenu(); };


    // the workspace panels
    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    mixerinput::openAudioSettings = [this] { openSettings ((int) SettingsPage::audio); };
    PatcherWindows::onBrowsePluginDatabase = [this] (bool) {
        showPanel (Panel::browser);
        browser.showPluginDatabaseForSnapshot(); // (its Generators and Effects folders open)
        if (auto* w = getTopLevelComponent())
            w->toFront (true);
    };
    rack.onOpenMixerInsert = [this] (int insert) { // double-click on a channel's route box
        showPanel (Panel::mixer);
        mixer.selectTrack (insert);
    };
    openMixerInsertHandler() = rack.onOpenMixerInsert; // and on the channel window's TRACK box (round 168s)
    mixer.onInstrumentDropped = [this] (int insert, ChannelDef c) { rack.addChannel (std::move (c), true, insert); };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);
    workspace.setContent (Panel::playlist, playlistArea, "Playlist", Icon::playlist);
    workspace.setContent (Panel::channelRack, rackViewport, "Channel Rack", Icon::channelRack);
    workspace.setContent (Panel::pianoRoll, pianoRoll, "Piano Roll", Icon::pianoRoll);
    workspace.setContent (Panel::mixer, mixer, "Mixer", Icon::mixer);
    workspace.setContent (Panel::browser, browser, "Browser", Icon::browser);
    workspace.setTitleTooltip (Panel::channelRack, tr ("Shift+wheel here changes the pattern"));
    // FL keeps the channel filter, the global swing and the graph editor switch in the rack's title bar
    rackFilterBox.text = rack.filterName();
    rackFilterBox.onClick = [this] { rack.showFilterMenu (&rackFilterBox); };
    rack.onFilterChanged = [this] {
        rackFilterBox.text = rack.filterName();
        rackFilterBox.repaint();
        rackGraphButton.setToggleState (rack.areGraphLanesShown(), juce::dontSendNotification);
        rackPianoRollButton.setToggleState (rack.isPianoRollView(), juce::dontSendNotification);
        rackLoopButton.setToggleState (rack.isLoopingOn(), juce::dontSendNotification);
    };
    rackGraphButton.setClickingTogglesState (false);
    rackGraphButton.onClick = [this] { rack.toggleGraphLanes(); };
    // the step sequencer / piano-roll view switch (FL keeps it in the rack's title bar too)
    rackPianoRollButton.setClickingTogglesState (false);
    rackPianoRollButton.setOnColour (theme::colour::bg6);
    rackPianoRollButton.setIconColourWhenOn (theme::colour::accent);
    rackPianoRollButton.onClick = [this] { runAction ("rack.pianoRollView"); };
    ui::setHint (rackPianoRollButton, tr ("Piano-roll view: every channel shows its notes as a miniature piano roll (click one to edit it) "
                                          "instead of the step buttons; click again for the steps"));
    rackTitleTools.addAndMakeVisible (rackPianoRollButton);
    // Follow (issue #12): a long pattern pages along with the playing step
    rackFollowButton.setClickingTogglesState (false);
    rackFollowButton.setOnColour (theme::colour::bg6);
    rackFollowButton.setIconColourWhenOn (theme::colour::accent);
    rackFollowButton.setWantsKeyboardFocus (false);
    rackFollowButton.setMouseClickGrabsKeyboardFocus (false);
    rackFollowButton.setToggleState (EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::channelRack), juce::dontSendNotification);
    rackFollowButton.setTooltip (tr ("Follow: a pattern longer than the Channel Rack pages along with the playing step; off, it stays where you scrolled"));
    ui::setQuickTip (rackFollowButton, tr ("Follow the playhead"));
    rackFollowButton.onClick = [this] { runAction ("rack.follow"); };
    rackTitleTools.addAndMakeVisible (rackFollowButton);
    // FL's Loop button (round 165f-rack): click switches the pattern's channel looping, right-click its loop type and burns
    rackLoopButton.setClickingTogglesState (false);
    rackLoopButton.setOnColour (theme::colour::bg6);
    rackLoopButton.setIconColourWhenOn (theme::colour::accent);
    rackLoopButton.onClick = [this] { rack.toggleLooping(); };
    rackLoopButton.onRightClick = [this] { rack.showLoopMenu (&rackLoopButton); };
    ui::setHint (rackLoopButton, tr ("Channel looping: channels shorter than the pattern repeat to its end (polyrhythms)  [LMB] on / off  "
                                     "[RMB] loop type (step channels, all channels, advanced) and Burn to pattern"));
    rackTitleTools.addAndMakeVisible (rackLoopButton);
    // Loop Starter (round 168b; FL 25's dice in the Channel Rack): a drum loop of the genre's kit, re-rolled with dice
    rackLoopStarterButton.setWantsKeyboardFocus (false);
    rackLoopStarterButton.onClick = [this] { loopstarterui::open (engine); };
    ui::setHint (rackLoopStarterButton, tr ("Loop Starter: pick a genre and get a drum loop (kit channels with steps) to re-roll and send to the Playlist"));
    rackTitleTools.addAndMakeVisible (rackLoopStarterButton);
    // the pattern selector (issue #9): change the pattern the rack edits right from its title bar
    rackPatternSelector.onStep = [this] (int delta) { stepPatternFromSelector (delta); };
    rackPatternSelector.onMenu = [this] { showRackPatternList(); };
    ui::setHint (rackPatternSelector, tr ("Pattern the Channel Rack edits: [LMB] on the name: the list of patterns, on the arrows: previous / next  [Wheel] step") + "  " + tr ("[LMB] drag up / down: step through the patterns"));
    rackTitleTools.addAndMakeVisible (rackPatternSelector);
    rackTitleTools.addAndMakeVisible (rackFilterBox);
    rackTitleTools.addAndMakeVisible (swingSlider);
    // the pattern length in bars (issue #12)
    rackLengthBox.onSet = [this] (int bars) {
        const auto index = engine.getSong().currentPattern;
        const auto ticks = (std::int64_t) juce::jlimit (0, 999, bars) * ppq * juce::jmax (1, engine.getSong().beatsPerBar);
        if (index >= 0 && index < (int) engine.getSong().patterns.size() && engine.getSong().patterns[(std::size_t) index].lengthTicks != ticks)
            engine.editSong ([index, ticks] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = ticks; });
        refreshRackLengthBox();
    };
    rackLengthBox.onMenu = [this] { showPatternLengthMenu(); };
    ui::setHint (rackLengthBox, tr ("Pattern length in bars: [LMB] drag or [Wheel] change it  [2xLMB] automatic (from the notes)  [RMB] menu"));
    ui::setQuickTip (rackLengthBox, tr ("Pattern length (bars)"));
    rackTitleTools.addAndMakeVisible (rackLengthBox);
    rackTitleTools.addAndMakeVisible (rackGraphButton);
    rackTitleTools.layout = [this] {
        auto r = rackTitleTools.getLocalBounds();
        rackPatternSelector.setBounds (r.removeFromLeft (rackPatternSelectorWidth).reduced (0, 3));
        r.removeFromLeft (6);
        rackFilterBox.setBounds (r.removeFromLeft (rackFilterBoxWidth).reduced (0, 3));
        r.removeFromLeft (4);
        swingSlider.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (4);
        rackLengthBox.setBounds (r.removeFromLeft (rackLengthBoxWidth).reduced (0, 3));
        r.removeFromLeft (4);
        rackGraphButton.setBounds (r.removeFromLeft (24).reduced (0, 1));
        r.removeFromLeft (4);
        rackPianoRollButton.setBounds (r.removeFromLeft (24).reduced (0, 1));
        r.removeFromLeft (4);
        rackFollowButton.setBounds (r.removeFromLeft (24).reduced (0, 1));
        r.removeFromLeft (4);
        rackLoopButton.setBounds (r.removeFromLeft (24).reduced (0, 1));
        r.removeFromLeft (4);
        rackLoopStarterButton.setBounds (r.removeFromLeft (24).reduced (0, 1));
    };
    workspace.setTitleAccessory (Panel::channelRack, &rackTitleTools, rackTitleToolsWidth);
    workspace.setTitleAccessory (Panel::mixer, &mixer.getLayoutBox(), MixerLayoutBox::width); // FL's "Mixer layout" (issue #52)
    mixer.getStripsForSnapshot().onLayoutChanged = [this] {
        mixer.getLayoutBox().repaint();
        mixer.selectTrack (mixer.getStripsForSnapshot().getSelectedTrack()); // keep the selected strip in view
        saveUiSettings();
    };
    workspace.onPanelMenu = [this] (Panel panel, juce::PopupMenu& menu) { addPanelMenuItems (panel, menu); };
    workspace.onFocusChanged = [this] (Panel panel) {
        refreshPanelButtons();
        // another panel took over (a click, F5..F9): the Browser's search box gives the keys back (issue #9)
        if (panel != Panel::browser)
            browser.leaveSearchBox();
    };
    workspace.onKeyInDetachedWindow = [this] (const juce::KeyPress& key) { return keyPressed (key); };
    workspace.onTitleWheel = [this] (Panel panel, const juce::MouseEvent& ev, const juce::MouseWheelDetails& wheel) {
        if (panel == Panel::channelRack && ev.mods.isShiftDown())
            selectPattern (stepPattern (engine.getSong(), (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1));
    };
    workspace.onTitleRightClick = [this] (Panel panel, const juce::MouseEvent&) {
        if (panel == Panel::channelRack)
            showPatternMenu();
    };
    workspace.onDefaultLayout = [this] { fitChannelRack (true); };
    workspace.setPersistent (persistent);
    playlist.onOpenPattern = [this] { showPanel (Panel::channelRack); };
    playlist.onOpenPianoRoll = [this] {
        // edit a channel that plays in this pattern
        const auto& song = engine.getSong();
        const auto& notes = song.patterns[(std::size_t) song.currentPattern].notes;
        const auto channel = engine.getSelectedChannel();
        if (! notes.empty() && std::none_of (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }))
            engine.setSelectedChannel (notes.front().channel);
        openPianoRoll();
    };
    picker.onOpenPattern = [this] { showPanel (Panel::channelRack); };
    picker.onPatternMenu = [this] { makePatternMenu (picker.pickedPatterns()).showMenuAsync (juce::PopupMenu::Options().withMousePosition()); };
    picker.onNewPattern = [this] { newPatternButton.onClick(); };
    playlist.onRenamePattern = [this] (int p) { showRenamePattern (p); };
    playlist.onOpenInSampler = [this] (int channel) {
        // the audio clip's file in a new Sampler channel (to play it from notes), its settings open
        const auto& song = engine.getSong();
        if (channel < 0 || channel >= (int) song.channels.size())
            return;
        ChannelDef c;
        c.name = song.channels[(std::size_t) channel].name;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = song.channels[(std::size_t) channel].samplePath;
        c.colour = song.channels[(std::size_t) channel].colour;
        const auto added = rack.addChannel (c, false);
        showPanel (Panel::channelRack);
        rack.toggleEditor (added, false);
    };
    ui::setHint (pianoRoll, tr ("Piano Roll: [LMB] add a note, drag to move  [RMB] delete  [Ctrl][LMB] drag to select  [Ctrl][Wheel] zoom"));
    ui::setHint (playlist, tr ("Playlist: [LMB] paint the current pattern  [RMB] delete  [Ctrl][LMB] drag to select  [Ctrl][Wheel] zoom"));

    // recent files (stored in %APPDATA%\LYNX)
    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (PluginHost::settingsFolder().getChildFile ("recentes.txt").loadFileAsString());
    addAndMakeVisible (menuBar);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &playButton, &stopButton,
                                                             &recordButton, &recordMenuButton, &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton,
                                                             &blendButton, &tapTempoButton, &loopRecordButton, &stepEditButton, &punchButton, &tempoDisplay, &patternBox, &newPatternButton, &patternMenuButton, &pluginPickerButton, &timeDisplay,
                                                             &songPosition, &masterVolume, &masterPitch, &perfMeter, &chordPanel, &output, &hintBar, &tweakReadout, &workspace, &midiLight })
    {
        addAndMakeVisible (c);
        c->setMouseClickGrabsKeyboardFocus (false); // the PC keyboard stays with the main window
    }
    // the dancers of the project's dance channels (FL's Fruity Dance) over the workspace (round 165e)
    danceStage = std::make_unique<DanceStage> (engine);
    danceStage->setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (*danceStage);

    for (auto& b : panelButtons)
    {
        addAndMakeVisible (b);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setWantsKeyboardFocus (false);
    }

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &playButton, &stopButton,
                                                             &recordButton, &recordMenuButton, &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton,
                                                             &blendButton, &tapTempoButton, &loopRecordButton, &stepEditButton, &punchButton, &newPatternButton, &patternMenuButton, &pluginPickerButton })
        b->setWantsKeyboardFocus (false);

    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &recordMenuButton, &newPatternButton, &patternMenuButton,
                     &pluginPickerButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton, &blendButton, &tapTempoButton, &loopRecordButton, &stepEditButton, &punchButton })
        b->setFlat (true);

    // a light header: every toolbar button is flat; "on" is a faint wash, in the accent for the states that
    // matter while working (playing, recording, metronome, recording options)
    {
        using namespace theme;
        const auto onWash = colour::accent.withAlpha (0.24f), onIcon = colour::accentHover; // (round 170e3: a clearer "on")
        for (auto* b : { &playButton, &stopButton, &recordButton, &patternModeButton, &songModeButton })
            b->setFlat (true);
        playButton.setOnColour (onWash);
        playButton.setIconColourWhenOn (onIcon);
        recordButton.setOnColour (colour::record.withAlpha (0.2f));
        recordButton.setIconColourWhenOn (colour::record);
        for (auto* b : { &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton, &blendButton, &loopRecordButton, &stepEditButton, &punchButton,
                          &patternModeButton, &songModeButton })
        {
            b->setOnColour (onWash);
            b->setIconColourWhenOn (onIcon);
        }
        for (auto& b : panelButtons)
        {
            b.setOnColour (colour::textPrimary.withAlpha (0.08f));
            b.setIconColourWhenOn (colour::textPrimary);
        }
    }
    quickTips = std::make_unique<ui::QuickTipWindow>();

    ui::installSliderContract();
    // FL's Last tweaked and automation recording (REA round 143): every automatable control reports its moves
    ui::onTweak = [this] (const AutomationTarget& t) { engine.noteTweaked (t); };
    // Options › Theme › Startup sound: its Play button (issue #12)
    themeui::previewStartupSound = [this] (const juce::String& path) {
        std::shared_ptr<const SampleData> sound;
        if (path.isNotEmpty() && juce::File (path).existsAsFile())
            sound = engine.getSampleLibrary().load (path);
        engine.previewSample (sound != nullptr ? sound : makeStartupSample());
    };
    // the toolbar's last-tweaked read-out opens the parameter's own menu (FL's Tools › Last tweaked; round 162d)
    tweakReadout.onClick = [this] {
        makeLastTweakedMenu (0).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&tweakReadout));
    };
    // FL's "Edit events in piano roll" (REA round 146): the channel's own parameter selects its channel
    onEditEventsInPianoRoll = [this] (const AutomationTarget& t) {
        const auto onChannel = t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
                               || t.kind == AutomationTargetKind::channelParam;
        if (onChannel && t.index >= 0 && t.index < (int) engine.getSong().channels.size())
            engine.setSelectedChannel (t.index);
        openPianoRoll();
        pianoRoll.showEventLane (t);
    };
    PluginWindows::onGesture = [this] (const juce::AudioPluginInstance& plugin, int param, bool starting) {
        if (const auto t = engine.targetOfPlugin (plugin, param))
        {
            if (starting)
                engine.noteTweaked (*t, true);
            else
                engine.tweakReleased (*t);
        }
    };
    engine.nameAutomation = [this] (const AutomationTarget& t) { return describeAutomationTarget (engine, t).toStdString(); };
    addAndMakeVisible (gridBox); // global grid (editors)
    snapToggleButton.setFlat (true);
    snapToggleButton.setOnColour (juce::Colours::transparentBlack);
    snapToggleButton.setIconColourWhenOn (theme::colour::accent);
    snapToggleButton.setWantsKeyboardFocus (false);
    snapToggleButton.setMouseClickGrabsKeyboardFocus (false);
    snapToggleButton.setToggleState (EditorGlobals::get().getGridOption() != grid::noneOption, juce::dontSendNotification);
    snapToggleButton.onClick = [this] {
        EditorGlobals::get().toggleGrid();
        snapToggleButton.setToggleState (EditorGlobals::get().getGridOption() != grid::noneOption, juce::dontSendNotification);
    };
    addAndMakeVisible (snapToggleButton);
    engine.addChangeListener (this);
    setTypingPiano (typingPiano);
    refreshRecordOptionButtons();
    refreshToolbar();
    refreshPanelButtons();
    setToolbarLayout (toolbarLayout, false); // icon sizes and row height (loaded with the UI options)
    if (persistent && toolbarLayout.uiScale != 100)
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) toolbarLayout.uiScale / 100.0f);

    addChildComponent (chordNearHint);
    chordPanel.onMenu = [this] { showChordPanelMenu(); };
    PianoRollComponent::onSelectedKeys = [this] (const std::vector<int>& keys) {
        if (! chordPanel.isVisible())
            return;
        refreshChordOptions();
        chordReadout.selection (keys, juce::Time::getMillisecondCounter());
        auto text = juce::String::fromUTF8 (chordReadout.text().c_str());
        chordPanel.setText (chordReadout.lastWasNovelty() ? tr (text) : text);
    };

    setControlHints();
    fittedChannelCount = (int) engine.getSong().channels.size();
    hintBar.setHint ({ idleHint(), {}, false, false });
    setWantsKeyboardFocus (true);
    installDialogTextFocus(); // dialogs open with their text field focused (round 165q)
    setSize (1180, 642);
    startTimerHz (30);
    uiTraceMonitor.start ([this, last = std::make_shared<RtStats::Snapshot>()] {
        // the audio side of the second (the watchdog thread: atomics only)
        const auto rt = engine.getRtStats().snapshot();
        const auto calls = rt.callbacks - last->callbacks;
        juce::String text;
        text << "audio: callbacks " << juce::String ((juce::int64) calls) << ", over deadline " << juce::String ((juce::int64) (rt.overBudget - last->overBudget))
             << ", late " << juce::String ((juce::int64) (rt.lateCalls - last->lateCalls)) << ", max load since start " << juce::String (rt.maxLoad * 100.0, 0)
             << "%, cpu " << juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) << "%";
        *last = rt;
        return text;
    });

    // new or updated plugins since the last run: an incremental scan in the background (unchanged files are
    // skipped), a few seconds after start so it doesn't compete with loading; the first run asks in the wizard
    if (persistent && setupWizardDone() && backgroundScanAtStart() && platform::testMode())
        platform::testModeLog ("background plugin scan at start");
    else if (persistent && setupWizardDone() && backgroundScanAtStart())
        juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->engine.getPluginHost().startBackgroundScan();
        });

    // round 168t (the YouTube review, 168d #9: "How do we get back to the playlist? Where is the playlist?"): the first
    // three starts name the window buttons and their keys in the hint bar for a while
    if (persistent)
        juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->showWindowsTip();
        });

    // Online Sessions P2 (round 169b): live sessions
    live = std::make_unique<LiveSessionManager> (engine);
    liveui::attach (*live);
    live->onChange = [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
        {
            safe->repaint (safe->titleArea);
            safe->menuItemsChanged();
            safe->updateTitle(); // (the session's state is in the window title too: narrow layouts hide the badge)
        }
    };
    live->onJoined = [safe = juce::Component::SafePointer<MainComponent> (this)] (Song song) {
        if (safe == nullptr)
            return;
        const auto* host = song.session.find (session::hostTag);
        const auto hostName = host != nullptr ? juce::String::fromUTF8 (host->name.c_str()) : juce::String();
        safe->startNewProject (std::move (song), {});
        safe->projectName = hostName.isNotEmpty() ? tr ("Live session with {0}", { hostName }) : tr ("Live session");
        safe->updateTitle();
    };
    // round 169j: the host's LYNX SHARE restarted during a session: its journal's song becomes the open project again
    live->onResumed = [safe = juce::Component::SafePointer<MainComponent> (this)] (Song song) {
        if (safe == nullptr)
            return;
        const auto journal = LiveSessionManager::readJournal (LiveSessionManager::defaultJournal());
        safe->startNewProject (std::move (song), {});
        if (journal && journal->projectFile.existsAsFile())
        {
            safe->currentFile = journal->projectFile;
            safe->projectName = journal->projectFile.getFileNameWithoutExtension();
        }
        safe->updateTitle();
    };
    engine.onSessionUndoSkipped = [] (const juce::StringArray& skipped) {
        ui::postHint ({ tr ("Undo left alone what someone else changed since: {0}", { skipped.joinIntoString (", ") }), {} }, 4000);
    };
    juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->startLiveFromCommandLine();
    });

    // the keys a secondary window (tool, plugin, channel window) leaves unused run the global shortcuts, as in FL
    setSecondaryWindowKeyHandler ([safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::KeyPress& key) {
        return safe != nullptr && safe->keyFromSecondaryWindow (key);
    });
}

MainComponent::~MainComponent()
{
    engine.onSessionUndoSkipped = nullptr;
    PianoRollComponent::onSelectedKeys = nullptr;
    liveui::closeChat();
    live.reset(); // (a session still on ends: the others are told)
    setSecondaryWindowKeyHandler (nullptr);
    cancelProjectLoad(); // an open in progress uses the engine from its thread
    if (sessionLock != nullptr) // a clean exit: no "LYNX did not close properly" next time
        sessionMarker.deleteFile();
    mixerinput::openAudioSettings = nullptr;
    openMixerInsertHandler() = nullptr;
    PatcherWindows::onBrowsePluginDatabase = nullptr;
    EditorGlobals::get().onOptionsChanged = nullptr;
    PianoRollComponent::shortcutText = nullptr;
    saveUiSettings();
    workspace.saveState();
    ui::uninstallSliderContract();
    ui::onTweak = nullptr;
    themeui::previewStartupSound = nullptr;
    onEditEventsInPianoRoll = nullptr;
    PluginWindows::onGesture = nullptr;
    removeDialogTextFocus();
    engine.nameAutomation = nullptr;
    PluginWindows::closeAll(); // plugin UIs before the engine
    ParametricEqWindows::closeAll();
    closeAutomationEditors();
    closeChannelSettings();
    closeAllToolWindows(); // note tools, history, quick-add (they edit the song)
    engine.removeChangeListener (this);
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("MainComponent paint", true, g.getClipBounds());
    using namespace theme;
    g.fillAll (colour::bg1);

    // the toolbar: one band, its panels separated by hairlines
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    g.setColour (colour::bg3);
    g.fillRect (bar);
    g.setColour (colour::border);
    g.fillRect (bar.withHeight (1).withY (bar.getBottom() - 1));

    // round 170e3: each panel sits in its own quiet well (transport, master, recording, snap and pattern, windows,
    // file, meters), so the groups read at a glance; with "Separators" off, only hairlines between them
    if (toolbarLayout.separators) // (the toolbar menu's "Separators")
    {
        for (const auto& r : panelBounds)
            if (! r.isEmpty())
            {
                const auto well = r.toFloat().reduced (1.0f, 2.0f);
                g.setColour (colour::bg1.withAlpha (0.55f));
                g.fillRoundedRectangle (well, radius::lg);
                g.setColour (colour::border);
                g.drawRoundedRectangle (well.reduced (0.5f), radius::lg, 1.0f);
            }
    }
    else
    {
        g.setColour (colour::border.brighter (0.08f));
        for (auto x : dividers)
            g.fillRect (x, bar.getY() + 12, 1, bar.getHeight() - 24);
    }

    // Pattern / Song: one switch of two halves (round 170e3), the playing mode lit
    if (! modeArea.isEmpty())
    {
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (modeArea.toFloat(), radius::lg);
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (modeArea.toFloat().reduced (0.5f), radius::lg, 1.0f);
    }

    // play, stop and record share one quiet well
    if (! transportArea.isEmpty())
    {
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (transportArea.toFloat(), radius::lg);
        g.setColour (colour::border);
        g.drawRoundedRectangle (transportArea.toFloat().reduced (0.5f), radius::lg, 1.0f);
    }

    // the hint bar sits in an inset well under the menu
    if (! hintBar.getBounds().isEmpty())
    {
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (hintBar.getBounds().toFloat(), radius::md);
    }

    // the project name, beside the menus (FL's title/hint panel: the title above the hint bar)
    if (! titleArea.isEmpty())
    {
        // the people of a live session first (round 169b), the project's name after them
        const auto people = live != nullptr ? liveui::paintPresence (g, titleArea, *live) : 0;
        const auto name = projectName.isNotEmpty() ? projectName : tr ("Untitled");
        g.setFont (font (type::small, true));
        g.setColour (colour::textSecondary);
        if (const auto rest = titleArea.withTrimmedLeft (people > 0 ? people + 10 : 0); rest.getWidth() >= 50)
            g.drawFittedText ((engine.isModified() ? juce::String ("* ") : juce::String()) + name, rest, juce::Justification::centredRight, 1, 0.8f);
    }

    // captions that name the snap and pattern selectors, after their icons (the magnet is a button)
    g.setFont (font (type::caption - 1.0f, true));
    g.setColour (colour::textDisabled);
    if (! snapLabelArea.isEmpty())
        if (snapLabelArea.getWidth() > rowHeight + 20) // (the narrowest toolbar keeps only the magnet)
            g.drawText (tr ("SNAP"), snapLabelArea.withTrimmedLeft (rowHeight + 1), juce::Justification::centredLeft, true);
    if (! patternLabelArea.isEmpty())
    {
        const auto iconBox = patternLabelArea.withWidth (rowHeight).toFloat();
        icons::draw (g, Icon::pattern, iconBox.withSizeKeepingCentre (14.0f, 14.0f), colour::textDisabled);
        if (patternLabelArea.getWidth() > rowHeight + 20)
            g.drawText (tr ("PATTERN"), patternLabelArea.withTrimmedLeft (rowHeight + 1), juce::Justification::centredLeft, true);
    }
}

void MainComponent::resized()
{
    using toolbarlayout::Group;
    if (const auto size = toolbarlayout::effectiveIconSize (toolbarLayout.iconSize, getWidth()); size != appliedIconSize)
        applyIconSizing (size);
    dividers.clear();
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    auto toolbar = bar.reduced (8, toolbarLayout.visible ? 5 : 3);
    const auto row1 = toolbar.getY(), row2 = toolbar.getY() + rowHeight + 2, h = rowHeight;
    auto big = h + 14;                                        // transport buttons: big, centred on both rows (round 170e3)
    auto bigWidth = h + 14;
    int gap = 17;                                             // a divider and the space around it (13 when compact)
    const auto grow = h - toolbarlayout::rowHeight (toolbarlayout::IconSize::medium); // wider panels for bigger rows

    // panel widths
    auto& lf = getLookAndFeel();
    auto measureMenu = [&] {
        int w = 0;
        for (const auto& name : getMenuBarNames())
            w += lf.getMenuBarItemWidth (menuBar, 0, name);
        return w;
    };
    menuBar.getProperties().set ("lynxTightMenuBar", false);
    int menuWidth = measureMenu();
    if (menuWidth > 390) // translated menu titles (German, Russian...): English stays as it was
    {
        menuBar.getProperties().set ("lynxTightMenuBar", true);
        menuWidth = measureMenu();
    }

    const auto modeWidth = juce::jmax (patternModeButton.getIdealWidth (h - 2), songModeButton.getIdealWidth (h - 2));
    int tempoWidth = 96 + grow, timeWidth = 140 + grow;
    const int gridWidth = 148;
    // the snap and pattern captions: an icon (the magnet button / the pattern icon) and the longer of the two names
    const juce::Font captionFont (theme::font (theme::type::caption - 1.0f, true));
    int captionWidth = h + 3 + juce::jlimit (30, 80, juce::roundToInt (juce::jmax (juce::GlyphArrangement::getStringWidth (captionFont, tr ("SNAP")),
                                                                                           juce::GlyphArrangement::getStringWidth (captionFont, tr ("PATTERN"))))) + 6;

    std::array<int, toolbarlayout::groupCount> widths {};
    const int tapWidth = juce::roundToInt (h * 0.8f); // the tempo tapper, right of the tempo (round 170e3)
    widths[(std::size_t) Group::transport] = modeWidth + 8 + 3 * (bigWidth + 4) + 4 + tempoWidth + 2 + tapWidth + 4 + timeWidth;
    widths[(std::size_t) Group::master] = 2 * (h + 26) + 4; // two big knobs side by side (round 170e3)
    widths[(std::size_t) Group::record] = 5 * h + 8; // five per row (round 170e3)
    widths[(std::size_t) Group::snap] = captionWidth + gridWidth;
    widths[(std::size_t) Group::windows] = 3 * h + 4;
    widths[(std::size_t) Group::file] = 3 * h + 4;
    widths[(std::size_t) Group::meters] = 118;
    widths[(std::size_t) Group::output] = 116 + grow * 2;
    widths[(std::size_t) Group::chord] = 96; // (FL's panel: 96 px)

    const auto room = toolbar.getWidth();
    // room for most hints on two lines; on a very wide window the hint bar takes more (round 170e3)
    auto minHint = juce::jmax (menuWidth + 8, 320);
    const auto maxHint = room >= 3000 ? 1000 : 620;
    auto visible = toolbarlayout::fit (toolbarLayout, widths, gap, room, minHint);
    // round 181b: the Chord detection panel is on the default toolbar, as in FL. At 1920 px it misses by about 90 px:
    // tighter displays and gaps make the room (they get wider again when there is more); all the panels stay
    if (toolbarLayout.visible && toolbarLayout.isShown (Group::chord) && ! visible[(std::size_t) Group::chord] && visible[(std::size_t) Group::record])
    {
        auto tighter = widths;
        tighter[(std::size_t) Group::transport] -= 12 + 36; // the tempo and the song position, as narrow as the compact ones
        tighter[(std::size_t) Group::snap] -= 16;
        tighter[(std::size_t) Group::chord] = 88;
        if (const auto fits = toolbarlayout::fit (toolbarLayout, tighter, 13, room, minHint); fits[(std::size_t) Group::chord])
        {
            widths = tighter;
            tempoWidth -= 12;
            timeWidth -= 36;
            gap = 13;
            visible = fits;
        }
    }
    // round 170e3: rather than dropping the recording switches on a narrow window (1280-1440 px), the panels get
    // tighter: four switches per row (the typing piano and the MIDI light go; Ctrl+T and the View menu keep the piano),
    // a narrower snap box and displays
    compactRecord = false;
    narrowSnap = false;
    if (toolbarLayout.visible && toolbarLayout.isShown (Group::record) && ! visible[(std::size_t) Group::record])
    {
        compactRecord = true;
        const auto before = 3 * (bigWidth + 4) + tempoWidth + timeWidth;
        big = h + 6;
        bigWidth = h + 4;
        tempoWidth = 84 + grow;
        timeWidth = 104 + grow;
        widths[(std::size_t) Group::transport] -= before - (3 * (bigWidth + 4) + tempoWidth + timeWidth);
        widths[(std::size_t) Group::master] = 92; // the knobs over each other, as before
        widths[(std::size_t) Group::record] = 4 * h + 6;
        widths[(std::size_t) Group::snap] -= 16;
        gap = 13;
        visible = toolbarlayout::fit (toolbarLayout, widths, gap, room, minHint);
        // round 170e5: still too narrow (1280 px): the snap and pattern captions keep only their icons, the hint
        // column only the menu's width, tighter displays and gaps; the recording switches stay
        if (! visible[(std::size_t) Group::record])
        {
            const auto icons = h + 4;
            widths[(std::size_t) Group::snap] -= captionWidth - icons;
            captionWidth = icons;
            tempoWidth -= 6;
            timeWidth -= 8;
            widths[(std::size_t) Group::transport] -= 14;
            minHint = menuWidth + 8;
            gap = 11;
            visible = toolbarlayout::fit (toolbarLayout, widths, gap, room, minHint);
        }
        // and still (1280 px, measured: the switches missed it by a few px): a narrower snap box, tight gaps; then
        // the window buttons go before the recording switches (F5-F9 and the View menu open the same windows)
        if (! visible[(std::size_t) Group::record])
        {
            widths[(std::size_t) Group::snap] -= 24;
            narrowSnap = true;
            gap = 8;
            visible = toolbarlayout::fit (toolbarLayout, widths, gap, room, minHint);
        }
        if (! visible[(std::size_t) Group::record])
        {
            auto narrow = toolbarLayout;
            for (auto& e : narrow.entries)
                if (e.group == Group::windows)
                    e.shown = false;
            visible = toolbarlayout::fit (narrow, widths, gap, room, minHint);
        }
    }
    if (! toolbarLayout.visible) // Ctrl+F11: only the menu and the hint bar, on one row
        visible.fill (false);
    panelBounds = {};
    int used = 0;
    for (int i = 0; i < toolbarlayout::groupCount; ++i)
        used += visible[(std::size_t) i] ? widths[(std::size_t) i] + gap : 0;

    auto hintWidth = juce::jlimit (juce::jmin (minHint, juce::jmax (160, room - used)), maxHint, room - used);
    // round 170e3: the spare room goes to useful things first (a wider output monitor, snap and pattern boxes, song
    // position and hint bar), then a little air between the panels, so no dead zone opens anywhere on a wide window
    auto spare = juce::jmax (0, room - used - hintWidth);
    auto grant = [&spare] (int share, int most) {
        const auto g = juce::jmax (0, juce::jmin (most, spare * share / 100));
        spare -= g;
        return g;
    };
    if (visible[(std::size_t) Group::output])
        widths[(std::size_t) Group::output] += grant (40, 900);
    if (visible[(std::size_t) Group::snap])
        widths[(std::size_t) Group::snap] += grant (20, 220);
    if (const auto more = grant (15, 120); more > 0)
    {
        timeWidth += more;
        widths[(std::size_t) Group::transport] += more;
    }
    hintWidth += grant (25, 500);
    const auto shownGroups = (int) std::count (visible.begin(), visible.end(), true);
    // at most twice the normal gap between the panels; what is still left goes to the output monitor and the hint bar
    const auto extra = toolbarlayout::extraGap (spare, shownGroups, gap);
    spare -= extra * shownGroups;
    if (spare > 0)
    {
        const auto toOutput = visible[(std::size_t) Group::output] ? spare / 2 : 0;
        widths[(std::size_t) Group::output] += toOutput;
        hintWidth += spare - toOutput;
        spare = 0;
    }

    auto take = [&toolbar] (int width) { return toolbar.removeFromLeft (juce::jmin (width, toolbar.getWidth())); };
    auto divide = [&] {
        take (gap / 2 + extra / 2);
        dividers.push_back (toolbar.getX());
        take (gap - gap / 2 + extra - extra / 2);
    };
    auto hide = [] (std::initializer_list<juce::Component*> components) {
        for (auto* c : components)
        {
            c->setVisible (false);
            c->setBounds ({});
        }
    };
    auto put = [] (juce::Component& c, juce::Rectangle<int> r) {
        c.setVisible (true);
        c.setBounds (r);
    };
    // a block of icon buttons, three per row on both rows
    auto grid = [&] (juce::Rectangle<int> panel, std::initializer_list<juce::Component*> buttons, int columns = 3) {
        int i = 0;
        for (auto* b : buttons)
        {
            const auto x = panel.getX() + (i % columns) * (h + 2);
            put (*b, juce::Rectangle<int> (x, i < columns ? row1 : row2, h, h));
            ++i;
        }
    };

    // 1. the menu (and the project name beside it) over the hint bar; always first
    if (! toolbarLayout.visible)
    {
        const auto row = toolbar.withHeight (h);
        const auto menu = row.withWidth (juce::jmin (menuWidth + 8, row.getWidth()));
        put (menuBar, menu);
        titleArea = {};
        placeHintBar (row.withTrimmedLeft (menu.getWidth() + 12).withWidth (juce::jmin (900, row.getWidth() - menu.getWidth() - 12)).reduced (0, 1));
        fixedToolbarArea = bar;
    }
    else
    {
        auto column = take (hintWidth);
        const auto menu = column.withY (row1).withHeight (h).removeFromLeft (juce::jmin (menuWidth + 8, column.getWidth()));
        put (menuBar, menu);
        titleArea = column.withY (row1).withHeight (h).withTrimmedLeft (menu.getWidth() + 12).withTrimmedRight (4);
        if (titleArea.getWidth() < 60)
            titleArea = {};
        placeHintBar (column.withY (row2).withHeight (h).reduced (0, 1));
        fixedToolbarArea = juce::Rectangle<int> (column.getX() - 4, 0, column.getWidth() + 8, toolbarHeight);
    }

    snapLabelArea = patternLabelArea = transportArea = modeArea = {};

    for (const auto& entry : toolbarLayout.entries)
    {
        const auto group = entry.group;

        if (! visible[(std::size_t) group])
        {
            switch (group)
            {
                case Group::transport:
                    hide ({ &patternModeButton, &songModeButton, &playButton, &stopButton, &recordButton, &tempoDisplay, &tapTempoButton, &timeDisplay, &songPosition });
                    break;
                case Group::master: hide ({ &masterVolume, &masterPitch }); break;
                case Group::record: hide ({ &metronomeButton, &typingPianoButton, &recordMenuButton, &countInButton, &waitInputButton, &blendButton, &loopRecordButton, &stepEditButton, &punchButton, &midiLight }); break;
                case Group::snap: hide ({ &gridBox, &snapToggleButton, &patternBox, &newPatternButton, &patternMenuButton }); break;
                case Group::windows:
                    for (auto& b : panelButtons)
                        hide ({ &b });
                    hide ({ &pluginPickerButton });
                    break;
                case Group::file: hide ({ &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton }); break;
                case Group::meters: hide ({ &perfMeter }); break;
                case Group::output: hide ({ &output }); break;
                case Group::chord: hide ({ &chordPanel }); break;
                case Group::count_: break;
            }
            continue;
        }

        if (toolbarlayout::rightAligned (group) && spare > 0)
        {
            take (spare);
            spare = 0;
        }

        divide();
        auto panel = take (widths[(std::size_t) group]);
        panelBounds[(std::size_t) group] = juce::Rectangle<int> (panel.getX() - 5, 0, panel.getWidth() + 10, toolbarHeight);

        switch (group)
        {
            case Group::transport: // Pattern/Song switch, play, stop, record, tempo, time display with the song position under it
            {
                auto mode = panel.removeFromLeft (modeWidth);
                modeArea = mode.withY (row1).withHeight (2 * h + 2).expanded (3, 2);
                put (patternModeButton, mode.withY (row1).withHeight (h).reduced (0, 1));
                put (songModeButton, mode.withY (row2).withHeight (h).reduced (0, 1));
                panel.removeFromLeft (8);
                const auto y = panel.getCentreY() - big / 2;

                transportArea = juce::Rectangle<int> (panel.getX(), y, 3 * bigWidth + 2 * 4, big);
                for (auto* b : { &playButton, &stopButton, &recordButton })
                {
                    put (*b, juce::Rectangle<int> (panel.getX(), y, bigWidth, big).reduced (0, 3));
                    panel.removeFromLeft (bigWidth + 4);
                }

                panel.removeFromLeft (4);
                const auto displays = juce::Rectangle<int> (panel.getX(), row1 + 3, panel.getWidth(), 2 * h - 6);
                const auto displayHeight = displays.getHeight() - 10;
                put (tempoDisplay, displays.withWidth (tempoWidth).withHeight (displayHeight));
                put (tapTempoButton, displays.withTrimmedLeft (tempoWidth + 2).withWidth (tapWidth).withHeight (displayHeight));
                put (timeDisplay, displays.withTrimmedLeft (tempoWidth + 2 + tapWidth + 4).withWidth (timeWidth).withHeight (displayHeight));
                put (songPosition, displays.withTop (displays.getY() + displayHeight + 1).withWidth (tempoWidth + 2 + tapWidth + 4 + timeWidth));
                break;
            }
            case Group::master: // master volume beside master pitch, each a big knob with its name and value under it
            {
                if (compactRecord)
                {
                    put (masterVolume, panel.withY (row1).withHeight (h).reduced (0, 1));
                    put (masterPitch, panel.withY (row2).withHeight (h).reduced (0, 1));
                    break;
                }
                const auto both = panel.withY (row1).withHeight (2 * h + 2);
                put (masterVolume, both.withWidth (both.getWidth() / 2 - 2));
                put (masterPitch, both.withTrimmedLeft (both.getWidth() / 2 + 2));
                break;
            }
            case Group::record: // the recording switches (round 170e3, as FL's recording panel): metronome, wait for input, count-in,
                                // blend and the typing piano over loop record, step edit, punch, the recording options menu and the MIDI light
                if (compactRecord)
                {
                    hide ({ &typingPianoButton, &midiLight });
                    grid (panel, { &metronomeButton, &waitInputButton, &countInButton, &blendButton,
                                   &loopRecordButton, &stepEditButton, &punchButton, &recordMenuButton }, 4);
                }
                else
                    grid (panel, { &metronomeButton, &waitInputButton, &countInButton, &blendButton, &typingPianoButton,
                                   &loopRecordButton, &stepEditButton, &punchButton, &recordMenuButton, &midiLight }, 5);
                break;
            case Group::snap: // global snap over the pattern selector, each with its name
            {
                auto first = juce::Rectangle<int> (panel.getX(), row1, panel.getWidth(), h);
                snapLabelArea = first.removeFromLeft (captionWidth);
                put (snapToggleButton, snapLabelArea.withWidth (h));
                put (gridBox, first.reduced (0, 1));
                auto second = juce::Rectangle<int> (panel.getX(), row2, panel.getWidth(), h);
                patternLabelArea = second.removeFromLeft (captionWidth);
                put (patternMenuButton, second.removeFromRight (h));
                if (narrowSnap) // (the pattern box keeps room for a name)
                {
                    put (newPatternButton, patternLabelArea.withWidth (h));
                    patternLabelArea = {};
                }
                else
                {
                    put (newPatternButton, second.removeFromRight (h));
                    second.removeFromRight (2);
                }
                put (patternBox, second.reduced (0, 1));
                break;
            }
            case Group::windows: // the windows and the plugin picker
                grid (panel, { &panelButtons[(std::size_t) Panel::playlist], &panelButtons[(std::size_t) Panel::channelRack],
                               &panelButtons[(std::size_t) Panel::pianoRoll], &panelButtons[(std::size_t) Panel::mixer],
                               &panelButtons[(std::size_t) Panel::browser], &pluginPickerButton });
                break;
            case Group::file: // new, open, save over export, undo, redo
                grid (panel, { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton });
                break;
            case Group::meters: put (perfMeter, panel.withY (row1).withHeight (2 * h + 2)); break;
            case Group::output: put (output, panel.withY (row1).withHeight (2 * h + 2)); break;
            case Group::chord: put (chordPanel, panel.withY (row1).withHeight (2 * h + 2)); break;
            case Group::count_: break;
        }
    }

    // the toolbar's edit mode: its overlay over the toolbar, and its tray between the toolbar and the windows
    const auto tray = toolbarEditor != nullptr ? toolbar::EditOverlay::trayHeight : 0;
    if (toolbarEditor != nullptr)
    {
        toolbarEditor->setBounds (0, 0, getWidth(), toolbarHeight + tray);
        toolbarEditor->setToolbarHeight (toolbarHeight);
        toolbarEditor->toFront (false);
    }
    auto below = getLocalBounds().withTrimmedTop (toolbarHeight + tray);
    if (updateBar.isVisible())
        updateBar.setBounds (below.removeFromTop (updater::UpdateBar::height));
    workspace.setBounds (below);
    if (danceStage != nullptr)
    {
        danceStage->setBounds (getLocalBounds());
        danceStage->toFront (false);
    }
    if (setupOverlay != nullptr)
    {
        setupOverlay->setBounds (getLocalBounds());
        setupOverlay->toFront (false);
    }
}

// --- toolbar layout ------------------------------------------------------------------------------

// The toolbar's rows and icons, and the editors' tool strips, for one icon size (round 170e3: the size in effect can be
// one step above the chosen one on a very wide window, toolbarlayout::effectiveIconSize).
void MainComponent::applyIconSizing (toolbarlayout::IconSize size)
{
    appliedIconSize = size;
    rowHeight = toolbarlayout::rowHeight (size);
    toolbarHeight = toolbarLayout.visible ? 2 * rowHeight + 12 : rowHeight + 6; // (round 170e3: room for the panel wells)

    const auto fraction = toolbarlayout::iconFraction (size), maxPx = toolbarlayout::iconMaxPx (size);
    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &recordMenuButton, &newPatternButton,
                     &patternMenuButton, &pluginPickerButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton, &snapToggleButton,
                     &blendButton, &tapTempoButton, &loopRecordButton, &stepEditButton, &punchButton })
        b->setIconSizing (fraction, maxPx);
    for (auto& b : panelButtons)
        b.setIconSizing (fraction, maxPx);
    for (auto* b : { &playButton, &stopButton, &recordButton }) // solid transport glyphs on big buttons (round 170e3)
        b->setIconSizing (0.42f, maxPx + 2.0f);

    ToolStrip::iconSize = size;
    pianoRoll.resized();
    playlist.resized();
}

void MainComponent::setToolbarLayout (const toolbarlayout::Layout& layout, bool save)
{
    const auto scaleChanged = layout.uiScale != toolbarLayout.uiScale;
    toolbarLayout = layout;
    applyIconSizing (toolbarlayout::effectiveIconSize (toolbarLayout.iconSize, getWidth()));

    if (save && scaleChanged)
    {
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) toolbarLayout.uiScale / 100.0f);
        // the windows grow with the zoom: keep their title bars and close buttons on the screen (round 163)
        juce::Timer::callAfterDelay (200, [] { fitAllWindowsOnScreen(); });
    }

    resized();
    repaint();
    if (toolbarEditor != nullptr)
        toolbarEditor->layoutChanged();

    if (save)
        saveUiSettings();
}

juce::PopupMenu MainComponent::makeToolbarMenu()
{
    using toolbarlayout::Group;
    juce::PopupMenu menu;
    // FL's toolbar menu (Edit, Separators, Hide, Presets), then LYNX's direct switches per panel
    menu.addSectionHeader (tr ("Toolbar"));
    menu.addItem (tr ("Edit toolbar (drag panels to move, add or remove them)"), true, toolbarEditor != nullptr,
                  [this] { setToolbarEditing (toolbarEditor == nullptr); });
    menu.addItem (tr ("Separators"), true, toolbarLayout.separators, [this] {
        auto layout = toolbarLayout;
        layout.separators = ! layout.separators;
        setToolbarLayout (layout);
    });
    menu.addItem (tr ("Hide the toolbar panels") + shortcutText ("view.toolbar"), true, ! toolbarLayout.visible, [this] {
        auto layout = toolbarLayout;
        layout.visible = ! layout.visible;
        setToolbarLayout (layout);
    });

    juce::PopupMenu presets;
    const auto current = toolbarlayout::matchingPreset (toolbarLayout);
    for (int p = 0; p < toolbarlayout::presetCount; ++p)
        presets.addItem (tr (toolbarlayout::presetName ((toolbarlayout::Preset) p)), true, current == p, [this, p] {
            auto layout = toolbarlayout::applyPreset (toolbarLayout, (toolbarlayout::Preset) p);
            layout.visible = true;
            setToolbarLayout (layout);
        });
    menu.addSubMenu (tr ("Presets"), presets);

    menu.addSectionHeader (tr ("Panels"));
    for (const auto& entry : toolbarLayout.entries)
    {
        const auto group = entry.group;
        menu.addItem (tr (toolbarlayout::groupName (group)), true, entry.shown, [this, group] {
            auto layout = toolbarLayout;
            layout.setShown (group, ! layout.isShown (group));
            layout.visible = true;
            setToolbarLayout (layout);
        });
    }

    menu.addSeparator();
    menu.addItem (tr ("Show detected MIDI chords near the hint bar"), true, chordShowNearHint, [this] {
        chordShowNearHint = ! chordShowNearHint;
        chordNearHint.setVisible (false);
        saveUiSettings();
    });
    menu.addSeparator();
    juce::PopupMenu sizes;
    const char* sizeNames[] = { TR_NOOP ("Small icons"), TR_NOOP ("Medium icons"), TR_NOOP ("Large icons") };
    for (int i = 0; i < 3; ++i)
        sizes.addItem (tr (sizeNames[i]), true, (int) toolbarLayout.iconSize == i, [this, i] {
            auto layout = toolbarLayout;
            layout.iconSize = (toolbarlayout::IconSize) i;
            setToolbarLayout (layout);
        });
    menu.addSubMenu (tr ("Icon size"), sizes);

    juce::PopupMenu scales;
    for (auto percent : toolbarlayout::uiScales)
        scales.addItem (juce::String (percent) + "%", true, toolbarLayout.uiScale == percent, [this, percent] {
            auto layout = toolbarLayout;
            layout.uiScale = percent;
            setToolbarLayout (layout);
        });
    menu.addSubMenu (tr ("UI scale"), scales);

    menu.addSeparator();
    menu.addItem (tr ("Customize toolbar..."), [this] { showCustomizeToolbar(); });
    menu.addItem (tr ("Reset toolbar to default"), ! toolbarLayout.isDefault(), false, [this] {
        setToolbarLayout (toolbarlayout::Layout::makeDefault());
    });
    return menu;
}

void MainComponent::setToolbarEditing (bool editing)
{
    if (editing == (toolbarEditor != nullptr))
        return;

    if (! editing)
    {
        removeChildComponent (toolbarEditor.get());
        toolbarEditor.reset();
        resized();
        repaint();
        return;
    }

    if (! toolbarLayout.visible) // editing shows the panels
    {
        auto layout = toolbarLayout;
        layout.visible = true;
        setToolbarLayout (layout);
    }

    toolbar::EditOverlay::Host host;
    host.layout = [this] { return toolbarLayout; };
    host.setLayout = [this] (const toolbarlayout::Layout& layout) { setToolbarLayout (layout); };
    host.panels = [this] { return panelBounds; };
    host.fixedArea = [this] { return fixedToolbarArea; };
    host.snapshot = [this] (juce::Rectangle<int> area) {
        // the panel as it looks, without the edit overlay on it
        if (toolbarEditor != nullptr)
            toolbarEditor->setVisible (false);
        auto image = createComponentSnapshot (area, true);
        if (toolbarEditor != nullptr)
            toolbarEditor->setVisible (true);
        return image;
    };
    host.done = [safe = juce::Component::SafePointer<MainComponent> (this)] {
        // after the click that asked for it
        juce::MessageManager::callAsync ([safe] {
            if (safe != nullptr)
                safe->setToolbarEditing (false);
        });
    };
    toolbarEditor = std::make_unique<toolbar::EditOverlay> (std::move (host), toolbarHeight);
    addAndMakeVisible (*toolbarEditor);
    resized();
    repaint();
    if (isShowing())
        toolbarEditor->grabKeyboardFocus();
}

void MainComponent::flUndo()
{
    if (alternateUndo)
        undoRedo (false);
    else
        undoRedo (engine.canRedo()); // something undone: Ctrl+Z redoes the next step (FL: "undo/redo on the same shortcut")
}

void MainComponent::undoRedo (bool redo)
{
    // FL says in the hint bar what was undone and the undo level ("Undone: ...  Level 2/3")
    if (redo ? ! engine.canRedo() : ! engine.canUndo())
        return;
    const auto label = engine.getHistoryLabel (engine.getHistoryPosition() + (redo ? 1 : 0));
    if (redo)
        engine.redo();
    else
        engine.undo();
    ui::Hint h;
    h.text = (redo ? tr ("Redone: {0}", { tr (label.text, { juce::String::fromUTF8 (label.arg.c_str()) }) })
                   : tr ("Undone: {0}", { tr (label.text, { juce::String::fromUTF8 (label.arg.c_str()) }) }));
    h.value = tr ("Level {0}/{1}", { juce::String (engine.getHistoryPosition() + 1), juce::String (engine.getHistorySize()) });
    ui::postHint (h, 2500);
}

ui::Hint MainComponent::hintAt (juce::Point<int> p)
{
    ui::Hint h;
    if (p.y < toolbarHeight)
    {
        h.text = tr ("Toolbar: [RMB] toolbar menu (edit the panels: move, add, remove; presets; hide with Ctrl+F11)");
        h.hasMenu = true;
    }
    return h;
}

void MainComponent::showToolbarMenu()
{
    makeToolbarMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void MainComponent::showCustomizeToolbar()
{
    auto panel = std::make_unique<toolbar::CustomizePanel> (toolbarLayout);
    panel->onChange = [safe = juce::Component::SafePointer<MainComponent> (this)] (const toolbarlayout::Layout& layout) {
        if (safe != nullptr)
            safe->setToolbarLayout (layout);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (fitContentToScreen (std::move (panel)).release());
    options.dialogTitle = tr ("Customize toolbar");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    // a right-click on the toolbar's background (between the panels): show / hide groups, icon size, customize
    if (e.mods.isPopupMenu() && e.y < toolbarHeight)
        showToolbarMenu();
}

juce::Rectangle<int> MainComponent::getToolStripAreaForSnapshot (bool playlistStrip)
{
    auto& editor = playlistStrip ? (juce::Component&) playlist : (juce::Component&) pianoRoll;
    if (! editor.isVisible() || editor.getParentComponent() == nullptr || ! isParentOf (&editor))
        return {};
    const auto area = getLocalArea (&editor, editor.getLocalBounds());
    return area.withHeight (40).withWidth (juce::jmin (area.getWidth(), 900)).getIntersection (getLocalBounds());
}

// --- keys --------------------------------------------------------------------------------------

keys::Context MainComponent::focusContext() const
{
    switch (workspace.getFocused())
    {
        case Panel::playlist: return keys::Context::playlist;
        case Panel::channelRack: return keys::Context::channelRack;
        case Panel::pianoRoll: return keys::Context::pianoRoll;
        case Panel::mixer: return keys::Context::mixer;
        case Panel::browser: return keys::Context::browser;
    }
    return keys::Context::global;
}

void MainComponent::dragOperationStarted (const juce::DragAndDropTarget::SourceDetails&)
{
    // a drop on a window above this one (the Sampler's waveform, a plugin...) lands there, not on the panel under it
    matchWindowOrderToScreen();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto chord = chordFromKeyPress (key);

    // 1. the typing piano: the keys its layout takes (notes, FL's LOCK) win over the shortcuts; FL's FREE keys don't
    const auto typed = typingKey (chord);
    if (typed.taken)
    {
        const auto code = key.getKeyCode();
        if (! heldKeys.hasTag (code) && typingLaunches.count (code) == 0) // key repeat: the notes are already sounding
        {
            // FL sends the typing keyboard's notes as MIDI channel 1 (FUN_011828c0 -> FUN_01181620 channel 0): with
            // "MIDI channel used for playlist live mode" = 1 and Performance mode on, they launch the Playlist's clips
            // (what "Layout Performance mode" is for), else they play the selected channels
            if (engine.typingNotesLaunchClips())
            {
                for (auto note : typed.notes)
                    if (perf::Command c; typingPerformance.note (note, true, c))
                        engine.performanceCommand (c);
                typingLaunches[code] = typed.notes;
            }
            else
                for (auto c : engine.getSelectedChannels())
                    for (auto note : typed.notes) // (a chord layout's key plays several)
                        heldKeys.press (c, note, 0.8f, code);
        }
        return true;
    }
    const auto pianoKeys = typingPiano && ! isPlainKey (chord); // (a FREE plain key is a shortcut as with the piano off)

    // 2. a key the user gave an action himself (Options › Shortcuts) wins over the panels' built-in keys
    // (issue #12: Metronome on 'C' did nothing while the Playlist, whose Knife is 'C', had the focus)
    if (const auto own = keyMap.findUserBound (chord, focusContext(), pianoKeys); ! own.empty() && runAction (own))
        return true;

    // 3. the focused panel's own keys
    const auto focused = workspace.getFocused();
    const auto panelShown = workspace.isPanelVisible (focused);

    if (panelShown && focused == Panel::pianoRoll && pianoRoll.handleKey (key))
        return true;

    if (panelShown && focused == Panel::playlist && playlist.handleKey (key))
        return true;

    // 4. the key map
    const auto action = keyMap.find (chord, focusContext(), pianoKeys);
    return ! action.empty() && runAction (action);
}

bool MainComponent::isPlainKey (const keys::Chord& chord) noexcept
{
    return (chord.mods & (keys::ctrl | keys::alt | keys::shift)) == 0;
}

keys::TypingKey MainComponent::typingKey (const keys::Chord& chord) const
{
    return typingPiano && isPlainKey (chord) ? keys::typingPianoKey (chord.key, typingOctave, typingRoot, typingLayout) : keys::TypingKey {};
}

void MainComponent::releaseTypingLaunches (bool all)
{
    for (auto it = typingLaunches.begin(); it != typingLaunches.end();)
        if (all || ! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            for (auto note : it->second)
                if (perf::Command c; typingPerformance.note (note, false, c))
                    engine.performanceCommand (c);
            it = typingLaunches.erase (it);
        }
        else
            ++it;
}

bool MainComponent::keyFromSecondaryWindow (const juce::KeyPress& key)
{
    // (round 165q / issue #12) a text field keeps its typing keys; the rest runs the global actions
    const auto typingIntoText = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    const auto chord = chordFromKeyPress (key);
    const auto action = keyMap.findFromSecondaryWindow (chord, typingIntoText, typingPiano && (! isPlainKey (chord) || typingKey (chord).taken));
    return ! action.empty() && runAction (action);
}

bool MainComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (id == "rack.follow")
    {
        auto& g = EditorGlobals::get();
        g.setFollowingView (EditorGlobals::FollowView::channelRack, ! g.isFollowingView (EditorGlobals::FollowView::channelRack));
        rackFollowButton.setToggleState (g.isFollowingView (EditorGlobals::FollowView::channelRack), juce::dontSendNotification);
        return true;
    }
    if (id.rfind ("pianoRoll.", 0) == 0 && pianoRoll.runKeyAction (id)) return true; // tools, slide, channel keys
    if (id.rfind ("playlist.tool.", 0) == 0 && playlist.runKeyAction (id)) return true;

    if (is ("transport.playStop")) playStop();
    else if (is ("transport.pause")) playPause();
    else if (is ("transport.playFromStart")) playFromStart();
    else if (is ("transport.toggleMode")) toggleMode();
    else if (is ("transport.nextMarker")) engine.jumpToNextMarker (true);
    else if (is ("transport.previousMarker")) engine.jumpToNextMarker (false);
    else if (is ("view.typingPiano")) toggleTypingPiano();
    else if (is ("view.toolbar"))
    {
        auto layout = toolbarLayout;
        layout.visible = ! layout.visible;
        setToolbarLayout (layout);
    }
    else if (is ("panel.playlist")) togglePanel (Panel::playlist);
    else if (is ("panel.channelRack")) togglePanel (Panel::channelRack);
    else if (is ("panel.pianoRoll"))
    {
        togglePanel (Panel::pianoRoll);
        if (workspace.isPanelVisible (Panel::pianoRoll))
            pianoRoll.scrollToNotes();
    }
    else if (is ("panel.browser")) togglePanel (Panel::browser);
    else if (is ("panel.mixer")) togglePanel (Panel::mixer);
    else if (is ("panel.maximisePlaylist"))
    {
        workspace.toggleMaximised (Panel::playlist);
        refreshPanelButtons();
    }
    else if (is ("pattern.next")) selectPattern (stepPattern (engine.getSong(), 1));
    else if (is ("pattern.previous")) selectPattern (stepPattern (engine.getSong(), -1));
    else if (is ("pattern.nextEmpty")) nextEmptyPattern();
    else if (is ("pattern.firstEmpty")) { engine.editSong ([] (Song& s) { gotoFirstEmptyPattern (s); }); showRenamePattern (engine.getSong().currentPattern); }
    else if (is ("pattern.nextEmptyNoName")) engine.editSong ([] (Song& s) { gotoNextEmptyPattern (s); });
    else if (is ("pattern.delete")) deleteCurrentPattern();
    else if (is ("pattern.rename")) showRenamePattern (engine.getSong().currentPattern);
    else if (is ("pattern.insert")) insertPatternHere();
    else if (is ("pattern.clone")) clonePatternNow();
    else if (is ("pattern.moveUp")) movePatternNow (-1);
    else if (is ("pattern.moveDown")) movePatternNow (1);
    else if (id.rfind ("pattern.select", 0) == 0) selectPattern (std::stoi (id.substr (14)) - 1);
    else if (is ("view.pluginPicker")) togglePluginPicker();
    else if (is ("panel.quickAdd"))
    {
        // FL's F8 opens the plugin picker or closes it (FLEngine 0xebf510, round 167o)
        if (auto* open = findToolWindow ("quickadd"))
            closeToolWindowOf (*open->getContentComponent());
        else
            showQuickAdd();
    }
    else if (is ("edit.history")) showHistory();
    else if (is ("tools.stopSound")) engine.stopSound();
    else if (is ("browser.search"))
    {
        showPanel (Panel::browser);
        browser.focusSearch();
    }
    else if (is ("transport.record")) toggleRecording();
    else if (is ("record.quickAudio")) quickRecordAudio();
    else if (is ("transport.metronome"))
    {
        engine.getTransport().metronome.store (! engine.getTransport().metronome.load());
        refreshModeButton();
    }
    else if (is ("record.countIn"))
    {
        auto o = engine.getRecordOptions();
        o.countInBars = o.countInBars > 0 ? 0 : 1;
        engine.setRecordOptions (o);
        refreshRecordOptionButtons();
    }
    else if (is ("record.captureMidi")) // round 170d (Ableton's Capture MIDI, Logic's Capture as Recording)
    {
        const auto r = engine.captureMidi();
        if (r.notes > 0)
            ui::postHint ({ tr ("Capture MIDI: {0} notes put in the pattern", { juce::String (r.notes) }), {}, false, false }, 4000);
        else
            ui::postHint ({ tr ("Capture MIDI: nothing to capture (play some notes first, without recording)"), {}, false, false }, 4000);
    }
    else if (is ("record.waitForInput"))
    {
        auto o = engine.getRecordOptions();
        o.waitForInput = ! o.waitForInput;
        engine.setRecordOptions (o);
        refreshRecordOptionButtons();
    }
    else if (is ("record.blend") || is ("record.loop") || is ("record.punch"))
    {
        auto o = engine.getRecordOptions();
        if (is ("record.blend"))
            o.mode = o.mode == RecordMode::overdub ? RecordMode::replace : RecordMode::overdub;
        else if (is ("record.loop"))
            o.loopRecord = ! o.loopRecord;
        else
            o.punchLoopRegion = ! o.punchLoopRegion;
        engine.setRecordOptions (o);
        refreshRecordOptionButtons();
        saveUiSettings();
    }
    else if (is ("record.stepEdit"))
    {
        engine.setStepEdit (! engine.isStepEdit());
        stepSeekTick = stepPrevTick = -1.0; // starts at the position shown now
        refreshRecordOptionButtons();
    }
    else if (is ("rack.graphEditor")) rack.toggleGraphLanes();
    else if (is ("rack.pianoRollView"))
    {
        rack.setPianoRollView (! rack.isPianoRollView());
        saveUiSettings();
    }
    else if (is ("view.audioEditor")) openAudioEditorForSelection();
    else if (is ("channel.soloSelected")) rack.solo (engine.getSelectedChannel());
    else if (is ("view.tempoTapper")) openTempoTapper (engine);
    else if (is ("view.sampleBrowser")) toggleSampleBrowser();
    else if (is ("view.pluginDatabase"))
    {
        showPanel (Panel::browser);
        browser.showPluginDatabase();
    }
    else if (is ("view.midiSettings") || is ("view.projectInfo"))
    {
        // FL's F10 / F11 (round 167o, issue #23): the same key closes Settings showing that page; on another page it
        // switches in place
        const auto page = is ("view.midiSettings") ? SettingsPage::midi : SettingsPage::info;
        const auto shown = shownSettingsPage();
        switch (keys::settingsKeyPress (shown >= 0, shown, (int) page))
        {
            case keys::SettingsKey::close: closeSettings(); break;
            case keys::SettingsKey::switchPage: switchSettingsPage (page); break;
            case keys::SettingsKey::open: openSettings ((int) page); break;
        }
    }
    else if (is ("view.closeUnfocusedWindows")) closeUnfocusedWindows();
    else if (is ("view.alignChannelEditors")) alignAllChannelEditors (engine.getSelectedChannel());
    else if (is ("view.touchKeyboard")) toggleTouchKeyboard (engine);
    else if (is ("view.closePluginWindows") || is ("view.closeWindows"))
    {
        PluginWindows::closeAll();
        ParametricEqWindows::closeAll(); // every native effect / controller editor
        if (is ("view.closeWindows")) // FL's F12: the editors and tool windows too (LYNX's docked panels stay)
        {
            closeAutomationEditors();
            closeChannelSettings();
            closeAllToolWindows();
            closeHelp();
        }
    }
    else if (is ("rack.groupSelected")) rack.groupSelected();
    else if (is ("rack.rotateLeft")) rack.rotateSelected (-1);
    else if (is ("rack.rotateRight")) rack.rotateSelected (1);
    else if (is ("browser.previous")) browser.step (-1, false);
    else if (is ("browser.next")) browser.step (1, false);
    else if (is ("browser.swapPrevious")) browser.step (-1, true);
    else if (is ("browser.swapNext")) browser.step (1, true);
    else if (is ("browser.load")) browser.loadSelected();
    else if (is ("browser.closeFolder")) browser.openFolder (false);
    else if (is ("browser.openFolder")) browser.openFolder (true);
    else if (is ("browser.rename"))
    {
        if (! browser.renameSelectedPlugin()) // not a plugin: F2's global action (rename the pattern)
            showRenamePattern (engine.getSong().currentPattern);
    }
    else if (id.rfind ("pianoRoll.", 0) == 0) return pianoRoll.runAction (id);
    else if (id.rfind ("playlist.", 0) == 0) return playlist.runAction (id);
    else if (id.rfind ("mixer.", 0) == 0) return mixer.runAction (id);
    else if (is ("file.new")) newProject();
    else if (is ("file.open")) chooseFileToOpen();
    else if (is ("file.save")) save (false);
    else if (is ("file.saveAs")) save (true);
    else if (is ("file.saveNewVersion")) saveNewVersion();
    else if (is ("file.export")) exportAudio();
    else if (is ("file.exportMaster")) showMasterDialog (engine, currentFile.getParentDirectory(), projectName);
    else if (is ("file.exportMp3")) exportAudio (1);
    else if (is ("file.exportMidi")) exportMidiFile();
    else if (is ("tools.consolidateSelection")) consolidatePlaylistSelection (engine, playlist.getSelectedClips(), false);
    else if (is ("tools.consolidateSelectionSongStart")) consolidatePlaylistSelection (engine, playlist.getSelectedClips(), true);
    else if (is ("edit.undo")) flUndo();
    else if (is ("edit.undoBackwards")) undoRedo (false);
    else if (is ("edit.redo")) undoRedo (true);
    else if (is ("help.shortcuts"))
    {
        if (helpWindow != nullptr) // F1 again closes the sheet (issue #23)
            closeHelp();
        else
            showHelp();
    }
    else if (is ("rack.selectPrevious")) rack.selectRelative (-1, false);
    else if (is ("rack.selectNext")) rack.selectRelative (1, false);
    else if (is ("rack.extendUp")) rack.selectRelative (-1, true);
    else if (is ("rack.extendDown")) rack.selectRelative (1, true);
    else if (is ("rack.moveUp")) rack.moveSelected (-1);
    else if (is ("rack.moveDown")) rack.moveSelected (1);
    else if (is ("rack.selectAll")) rack.selectAll();
    else if (is ("rack.clone")) rack.cloneSelected();
    else if (is ("rack.deleteSelected")) rack.deleteSelected();
    else if (is ("rack.cut")) rack.cutSelected();
    else if (is ("rack.copy")) rack.copySelected();
    else if (is ("rack.paste")) rack.pasteSelected();
    else if (is ("rack.sendToInserts")) rack.sendSelectedToInserts();
    else if (id.rfind ("rack.mute", 0) == 0) rack.toggleMute (std::stoi (id.substr (9)) - 1);
    else if (id.rfind ("rack.solo", 0) == 0) rack.solo (std::stoi (id.substr (9)) - 1);
    else
        return false;

    return true;
}

bool MainComponent::keyStateChanged (bool)
{
    // JUCE has no per-key "keyReleased"; check which held keys were released (each held note remembers its
    // channel and key, so the selection changing meanwhile can't leave one sounding)
    std::vector<int> released;
    heldKeys.forEachTag ([&released] (int code) {
        if (! juce::KeyPress::isKeyCurrentlyDown (code))
            released.push_back (code);
    });

    for (auto code : released)
        heldKeys.releaseTag (code);
    releaseTypingLaunches (false);

    return false;
}

juce::String MainComponent::shortcutText (const char* actionId) const
{
    const auto& chords = keyMap.chordsFor (actionId);
    return chords.empty() ? juce::String() : "\t" + juce::String (keys::displayString (chords.front()));
}

void MainComponent::toggleTypingPiano()
{
    setTypingPiano (! typingPiano);
    // say what changed in the hint bar (issue #34: people did not find the typing piano; Ctrl+T gave no feedback)
    ui::Hint hint;
    hint.text = typingPiano ? tr ("Typing keyboard to piano: on (Z..M and Q..P play the selected channels)")
                            : tr ("Typing keyboard to piano: off (letter and number keys are shortcuts)");
    ui::postHint (hint, 2500);
}

void MainComponent::setTypingPiano (bool on)
{
    typingPiano = on;
    EditorGlobals::get().typingPiano = on; // the editors' single-letter tool keys only work while it is off
    typingPianoButton.setToggleState (on, juce::dontSendNotification);
    typingPianoButton.setTooltip ((on ? tr ("Typing keyboard to piano: on (Z..M and Q..P play the selected channels)")
                                      : tr ("Typing keyboard to piano: off (letter and number keys are shortcuts)"))
                                  + juce::String (" (") + juce::String (keys::displayString (keyMap.chordsFor ("view.typingPiano").empty() ? keys::Chord {}
                                                                                                                                 : keyMap.chordsFor ("view.typingPiano").front()))
                                  + ")");

    // release anything still sounding from the keyboard
    heldKeys.releaseAll();
    releaseTypingLaunches (true);
}

void MainComponent::loadKeyMap()
{
    keyMap.resetToDefaults();
    const auto file = PluginHost::settingsFolder().getChildFile ("keys.json");

    if (file.existsAsFile())
    {
        std::vector<std::string> errors;
        if (! keyMap.loadJson (file.loadFileAsString().toStdString(), &errors))
            juce::Logger::writeToLog ("keys.json is not a JSON object; using the default keys");
        for (const auto& e : errors)
            juce::Logger::writeToLog ("keys.json: " + juce::String (e));
    }

    auto keyOf = [this] (const std::string& id) {
        const auto& chords = keyMap.chordsFor (id);
        return chords.empty() ? juce::String() : juce::String (keys::displayString (chords.front()));
    };
    pianoRoll.refreshToolKeys (keyOf);
    playlist.refreshToolKeys (keyOf);
}

// --- Browsers ------------------------------------------------------------------------------------

// The main Browser and the separate Sample browser window (round 163) act on the project the same way.
void MainComponent::connectBrowser (BrowserComponent& b)
{
    b.onOpenProject = [this] (const juce::File& file) { confirmDiscard ([this, file] { openFile (file); }); };
    b.onLoadIntoChannel = [this] (const juce::File& file) { return rack.loadSampleIntoSelected (file); };
    b.onOpenMixerPreset = [this] (const juce::File& file) {
        if (! mixerpresets::isMixerPreset (file))
            return false;
        if (! mixer.loadPresetOnSelection (file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Open mixer track state"),
                                                    tr ("This file is not a mixer track state LYNX SHARE can read."));
        return true;
    };
    // the Browser's "Current project" items (REA_rodada115)
    b.onOpenPattern = [this] (int pattern) {
        selectPattern (pattern);
        openPianoRoll();
    };
    b.onPatternMenu = [this] (int pattern) {
        selectPattern (pattern);
        makePatternMenu().showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    };
    b.onOpenChannel = [this] (int channel) {
        engine.setSelectedChannel (channel);
        rack.toggleEditor (channel, true);
    };
    b.onOpenEffect = [this] (int track, int slot) {
        showPanel (Panel::mixer);
        mixer.openSlotEditor (track, slot);
    };
    b.onShowInsert = [this] (int track) {
        showPanel (Panel::mixer);
        mixer.selectTrack (track);
    };
    b.onOpenAutomation = [this] (int automation) { openAutomationEditor (engine, automation); };
    // the Browser's Plugin database (round 142): the same targets as the quick-add palette
    b.onAddInstrument = [this] (ChannelDef c) {
        if (PatcherWindows::takeInstrumentFromBrowser (c)) // a Patcher's Add plugin › Browse plugin database (round 162)
            return;
        rack.addChannel (std::move (c), true);
        showPanel (Panel::channelRack);
    };
    b.onAddEffect = [this] (EffectSlot slot) {
        if (PatcherWindows::takeEffectFromBrowser (slot))
            return;
        if (! mixer.addEffectToSelected (std::move (slot)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin database"),
                                                    tr ("The selected insert has no free effect slot."));
        else
            showPanel (Panel::mixer);
    };
    b.onPreviewVolumeChanged = [this] { saveUiSettings(); };
    b.onSendToPlaylist = [this] (const juce::File& file) {
        playlist.addAudioClipAtPlayhead (file);
        showPanel (Panel::playlist);
    };
    b.onToggleSampleBrowser = [this] { toggleSampleBrowser(); };
}

void MainComponent::toggleSampleBrowser()
{
    if (auto* w = findToolWindow ("sampleBrowser"))
    {
        w->closeButtonPressed(); // (hidden, then deleted: remembered as closed)
        return;
    }
    openSampleBrowser();
}

void MainComponent::openSampleBrowser()
{
    auto* window = openToolWindow (BrowserComponent::makeSampleBrowserContent (engine, [this] (BrowserComponent& b) { connectBrowser (b); }),
                                   tr ("Sample browser"), "sampleBrowser");
    if (window == nullptr)
        return;
    window->setResizable (true, false);
    window->setResizeLimits (240, 260, 4000, 4000);
    const auto saved = BrowserComponent::sampleBrowserBounds();
    const auto area = juce::Desktop::getInstance().getDisplays().getTotalBounds (true);
    if (saved.getWidth() >= 240 && saved.getHeight() >= 260 && area.intersects (saved))
        window->setBounds (saved);
    else if (auto* top = getTopLevelComponent())
        window->setBounds (top->getScreenBounds().getRight() - 360, top->getScreenY() + 80, 340, juce::jmax (400, top->getHeight() - 160));
}

// --- UI options ----------------------------------------------------------------------------------

juce::File MainComponent::uiSettingsFile() const
{
    return PluginHost::settingsFolder().getChildFile ("ui-options.txt");
}

void MainComponent::loadUiSettings()
{
    juce::StringArray lines;
    lines.addLines (uiSettingsFile().loadFileAsString());
    // the sound in FL's order (0.1.20) wins over the old key, wherever each line is
    const auto flMetronomeSound = std::any_of (lines.begin(), lines.end(), [] (const juce::String& l) { return l.trim().startsWith ("metronomeSoundFl"); });

    for (const auto& line : lines)
    {
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto on = line.fromFirstOccurrenceOf ("=", false, false).trim() == "1";

        if (key == "typingPiano")
            typingPiano = on;
        else if (key == "typingOctave")
            typingOctave = keys::typingpiano::clampOctave (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "typingRoot")
            typingRoot = keys::typingpiano::clampRoot (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "typingLayout") // FL's layout name, so the list can grow
            typingLayout = keys::typingpiano::layoutIndex (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "keepEditorsOpen")
            ChannelRackComponent::keepEditorsOpen = on;
        else if (key == "selFirstChanPatChanges")
            ChannelRackComponent::selectFirstChannelWithNotes = on;
        else if (key == "muteRemovedSteps")
            ChannelRackComponent::muteRemovedSteps = on;
        else if (key == "alternateUndo")
            alternateUndo = on;
        else if (key == "waveSpectral") // ("waveColours", the old spectral-by-default key, is ignored)
            ui::WaveformCache::spectral = on;
        else if (key == "pluginStyle") // "Automatic" or a style forced on every plugin
        {
            const auto v = line.fromFirstOccurrenceOf ("=", false, false).trim();
            skin::setForcedStyle (v.equalsIgnoreCase ("Automatic") || v.isEmpty() ? -1 : (int) skin::styleFromName (v));
        }
        else if (key == "pluginStyleOverrides")
            skin::overridesFromString (line.fromFirstOccurrenceOf ("=", false, false).trim());
        else if (key == "reducedEffects")
            skin::setReducedEffects (on);
        else if (key == "animationRate") // round 174e2: 0 = the display's refresh, else 60 / 30
        {
            const auto hz = line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue();
            ui::FrameTicker::rateLimit() = hz == 30 || hz == 60 ? hz : 0;
        }
        else if (key == "artMotion")
            skin::setArtMotion (on);
        else if (key == "waveStereo")
            ui::WaveformCache::stereo = on;
        else if (key == "noteNames")
            notenames::currentStyle.store (juce::jlimit (0, 2, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue()));
        else if (key == "noteFlats")
            notenames::currentFlats.store (on);
        else if (key == "noteNamesEveryKey")
            notenames::everyKey.store (on);
        else if (key == "rackPlayheadColumn")
            ChannelRackComponent::playheadColumn.store (on);
        else if (key == "rackStepsChannelColour")
            ChannelRackComponent::stepsChannelColour.store (on);
        else if (key == "rememberSeek")
            EditorGlobals::get().rememberSeek = on;
        else if (key == "createVariants")
            EditorGlobals::get().createVariants = on;
        else if (key == "prGridHighlights") // (round 168g: the Piano roll's View items)
            PianoRollComponent::gridHighlights = on;
        else if (key == "prShowChordName")
            PianoRollComponent::showChordName = on;
        else if (key == "prSongBars")
            PianoRollComponent::songBarsRuler = on;
        else if (key == "audioLogEnabled")
            engine.getRollingLog().setEnabled (on);
        else if (key == "audioLogSeconds")
        {
            const auto seconds = line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue();
            if (seconds == 60 || seconds == 120 || seconds == 300 || seconds == 600)
                engine.getRollingLog().setLengthSeconds (seconds);
        }
        else if (key == "audioLogPreEffects")
            engine.getRollingLog().setPosition (on ? RollingAudioLog::Position::preEffects : RollingAudioLog::Position::postEffects);
        else if (key == "chordDelayMs")
            chordReadout.setDelayMs ((std::uint32_t) line.fromFirstOccurrenceOf ("=", false, false).trim().getLargeIntValue());
        else if (key == "chordRoman")
        {
            auto o = chordReadout.getOptions();
            o.roman = on;
            chordReadout.setOptions (o);
        }
        else if (key == "chordNearHint")
            chordShowNearHint = on;
        else if (key == "stretchedClipsLoop")
            setStretchedClipsLoop (on);
        else if (key == "followPlaylist" || key == "followPianoRoll")
            EditorGlobals::get().setFollowing (key == "followPianoRoll", on, false);
        else if (key == "followChannelRack" || key == "followAudioEditor")
            EditorGlobals::get().setFollowingView (key == "followChannelRack" ? EditorGlobals::FollowView::channelRack
                                                                               : EditorGlobals::FollowView::audioEditor, on, false);
        else if (key == "scopeMode")
            toolbar::OutputMonitor::mode = juce::jlimit (0, toolbar::OutputMonitor::modeCount - 1,
                                                         line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "toolbarGroups")
            toolbarLayout.orderFromString (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "toolbarIcons")
            toolbarLayout.iconSize = toolbarlayout::Layout::iconSizeFromString (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "toolbarSeparators")
            toolbarLayout.separators = on;
        else if (key == "toolbarVisible")
            toolbarLayout.visible = on;
        else if (key == "menuDensity")
            theme::menus::setCompact (line.fromFirstOccurrenceOf ("=", false, false).trim() != "comfortable");
        else if (key == "uiScale")
            toolbarLayout.uiScale = toolbarlayout::clampScale (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "editPatternsIn")
            PlaylistComponent::editPatternsIn = juce::jlimit (0, 2, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "autosaveMinutes")
            autosaveMinutes = prefs::clampAutosaveMinutes (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "askUnusedRecordings")
            askUnusedRecordings = on;
        else if (key == "backupWaitsForQuiet")
            backupWaitsForQuiet = on;
        else if (key == "backupLongestWait")
            backupLongestWaitMinutes = prefs::clampBackupLongestWait (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "hoverValues")
            hoverValues = on;
        else if (key == "autosaveKeep")
            autosaveKeep = prefs::clampAutosaveKeep (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "metronomeSound" && ! flMetronomeSound) // (before 0.1.20: 0 LYNX click, 1..5 hi-hat..pulse)
            engine.getTransport().metronomeSound.store (metronome::soundFromLegacy (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue()));
        else if (key == "metronomeSoundFl") // FL's order: 0 Hi-hat, 1 Tick, 2 Beep, 3 Cowbell, 4 Pulse
            engine.getTransport().metronomeSound.store (metronome::clampSound (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue()));
        else if (key == "metronomeTrack")
            engine.getTransport().metronomeTrack.store (juce::jlimit (0, maxMixerTracks - 1, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue()));
        else if (key == "playTruncatedOnTransport")
            engine.getTransport().playTruncatedOnTransport.store (on);
        else if (key == "resetPluginsOnTransport")
            engine.getTransport().resetPluginsOnTransport.store (on);
        else if (key == "metronomeVolume")
            engine.getTransport().metronomeVolume.store ((float) controls::clampUtilityVolume (line.fromFirstOccurrenceOf ("=", false, false).trim().getDoubleValue()));
        else if (key == "previewVolume")
            engine.setPreviewVolume (line.fromFirstOccurrenceOf ("=", false, false).trim().getDoubleValue());
        else if (key == "editorVolume")
            engine.setEditorVolume (line.fromFirstOccurrenceOf ("=", false, false).trim().getDoubleValue());
        else if (key == "rackPianoRollView")
            rack.setPianoRollView (on);
        else if (key == "undoLevels")
            engine.setUndoLevels (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "askWhatToRecord" || key == "recordNotes" || key == "recordAudio" || key == "recordStartsPlayback" || key == "disarmOnStop"
                 || key == "recordOffOnStop" || key == "unarmAfterRender" || key == "renderAutoClip")
        {
            auto o = engine.getRecordOptions();
            (key == "askWhatToRecord" ? o.askWhatToRecord : key == "recordNotes" ? o.recordNotes : key == "recordAudio" ? o.recordAudio
             : key == "disarmOnStop" ? o.disarmOnStop : key == "recordOffOnStop" ? o.recordOffOnStop : key == "unarmAfterRender" ? o.unarmAfterRender
             : key == "renderAutoClip"                                                                                         ? o.autoCreateClip
                                                                                                                                  : o.startsPlayback) = on;
            engine.setRecordOptions (o);
        }
        else if (key == "recordAutomation" || key == "recordQuantiseAutomation" || key == "recordClips")
        {
            auto o = engine.getRecordOptions();
            (key == "recordAutomation" ? o.recordAutomation : key == "recordClips" ? o.recordClips : o.quantiseAutomation) = on;
            engine.setRecordOptions (o);
        }
        else if (key == "recordToStepSequencer" || key == "recordingMarkers" || key == "recordNoteEnd")
        {
            auto o = engine.getRecordOptions();
            if (key == "recordNoteEnd")
                o.noteEnd = (NoteEnd) juce::jlimit (0, 2, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
            else
                (key == "recordToStepSequencer" ? o.toStepSequencer : o.recordingMarkers) = on;
            engine.setRecordOptions (o);
        }
        else if (key == "recordCatchWindow" || key == "recordCompensateOutput") // (round 170d)
        {
            auto o = engine.getRecordOptions();
            const auto v = line.fromFirstOccurrenceOf ("=", false, false).trim().getLargeIntValue();
            if (key == "recordCatchWindow")
                o.catchWindow = juce::jlimit<std::int64_t> (0, ppq, v);
            else
                o.compensateOutput = v != 0;
            engine.setRecordOptions (o);
        }
        else if (key == "recordPunchLoop" || key == "recordPreRoll" || key == "recordPostRoll" || key == "recordLoop")
        {
            auto o = engine.getRecordOptions();
            const auto v = line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue();
            if (key == "recordPunchLoop")
                o.punchLoopRegion = v != 0;
            else if (key == "recordLoop")
                o.loopRecord = v != 0;
            else
                (key == "recordPreRoll" ? o.preRollBars : o.postRollBars) = juce::jlimit (0, 8, v);
            engine.setRecordOptions (o);
        }
        else if (key == "recordQuantiseStarts")
        {
            recordQuantiseOn = on;
            updateRecordQuantise();
        }
        else if (key == "newInserts" || key == "newMasterChain" || key == "newCleanMixer" || key == "playlistTracks" || key == "newAutoCrossfades")
            newProjectDefaults().readKey (key.toStdString(), line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "mixerUsedOnly")
            MixerStrips::showUsedOnly = on;
        else if (key == "mixerLayout") // FL's FXViewMode numbering (issue #52)
        {
            MixerStrips::layout = mixerlayout::parse (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
            mixer.getStripsForSnapshot().updateSize();
        }
        else if (key == "quantizeSongJumps")
            engine.quantizeSongJumps.store (juce::jlimit (0, 5, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue()));
    }

    startup = StartupOptions::load();
    browser.refreshPreviewVolume();
}

void MainComponent::saveUiSettings()
{
    if (! persistent)
        return;

    uiSettingsFile().replaceWithText ("typingPiano=" + juce::String (typingPiano ? 1 : 0) + "\ntypingOctave=" + juce::String (typingOctave)
                                      + "\ntypingRoot=" + juce::String (typingRoot)
                                      + "\ntypingLayout=" + juce::String (keys::typingpiano::layoutName (typingLayout)) + "\nkeepEditorsOpen="
                                      + juce::String (ChannelRackComponent::keepEditorsOpen ? 1 : 0)
                                      + "\nselFirstChanPatChanges=" + juce::String (ChannelRackComponent::selectFirstChannelWithNotes ? 1 : 0)
                                      + "\nmuteRemovedSteps=" + juce::String (ChannelRackComponent::muteRemovedSteps ? 1 : 0)
                                      + "\nalternateUndo=" + juce::String (alternateUndo ? 1 : 0)
                                      + "\neditPatternsIn="
                                      + juce::String (PlaylistComponent::editPatternsIn) + "\nsplash=" + juce::String (startup.splash ? 1 : 0)
                                      + "\nstartupSound=" + juce::String (startup.sound ? 1 : 0)
                                      + "\nwaveSpectral=" + juce::String (ui::WaveformCache::spectral ? 1 : 0)
                                      + "\nwaveStereo=" + juce::String (ui::WaveformCache::stereo ? 1 : 0)
                                      + "\nnoteNames=" + juce::String (notenames::currentStyle.load())
                                      + "\nnoteFlats=" + juce::String (notenames::currentFlats.load() ? 1 : 0)
                                      + "\nnoteNamesEveryKey=" + juce::String (notenames::everyKey.load() ? 1 : 0)
                                      + "\nrackPlayheadColumn=" + juce::String (ChannelRackComponent::playheadColumn.load() ? 1 : 0)
                                      + "\nrackStepsChannelColour=" + juce::String (ChannelRackComponent::stepsChannelColour.load() ? 1 : 0)
                                      + "\nreducedEffects=" + juce::String (skin::reducedEffects() ? 1 : 0)
                                      + "\nanimationRate=" + juce::String (ui::FrameTicker::rateLimit())
                                      + "\nartMotion=" + juce::String (skin::artMotion() ? 1 : 0)
                                      + "\npluginStyle=" + juce::String (skin::forcedStyle() < 0 ? "Automatic" : skin::styleName ((skin::Style) skin::forcedStyle()))
                                      + "\npluginStyleOverrides=" + skin::overridesToString()
                                      + "\nrememberSeek=" + juce::String (EditorGlobals::get().rememberSeek ? 1 : 0)
                                      + "\ncreateVariants=" + juce::String (EditorGlobals::get().createVariants ? 1 : 0)
                                      + "\nprGridHighlights=" + juce::String (PianoRollComponent::gridHighlights ? 1 : 0)
                                      + "\nprShowChordName=" + juce::String (PianoRollComponent::showChordName ? 1 : 0)
                                      + "\nprSongBars=" + juce::String (PianoRollComponent::songBarsRuler ? 1 : 0)
                                      + "\naudioLogEnabled=" + juce::String (engine.getRollingLog().isEnabled() ? 1 : 0)
                                      + "\naudioLogSeconds=" + juce::String (engine.getRollingLog().lengthSeconds())
                                      + "\naudioLogPreEffects=" + juce::String (engine.getRollingLog().getPosition() == RollingAudioLog::Position::preEffects ? 1 : 0)
                                      + "\nchordDelayMs=" + juce::String ((std::int64_t) chordReadout.getDelayMs())
                                      + "\nchordRoman=" + juce::String (chordReadout.getOptions().roman ? 1 : 0)
                                      + "\nchordNearHint=" + juce::String (chordShowNearHint ? 1 : 0)
                                      + "\nstretchedClipsLoop=" + juce::String (stretchedClipsLoop() ? 1 : 0)
                                      + "\nfollowPlaylist=" + juce::String (EditorGlobals::get().isFollowing (false) ? 1 : 0)
                                      + "\nfollowPianoRoll=" + juce::String (EditorGlobals::get().isFollowing (true) ? 1 : 0)
                                      + "\nfollowChannelRack=" + juce::String (EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::channelRack) ? 1 : 0)
                                      + "\nfollowAudioEditor=" + juce::String (EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::audioEditor) ? 1 : 0)
                                      + "\nscopeMode=" + juce::String (toolbar::OutputMonitor::mode)
                                      + "\ntoolbarGroups=" + juce::String (toolbarLayout.orderToString())
                                      + "\ntoolbarIcons=" + toolbarlayout::Layout::iconSizeId (toolbarLayout.iconSize)
                                      + "\nuiScale=" + juce::String (toolbarLayout.uiScale)
                                      + "\nmenuDensity=" + juce::String (theme::menus::compact() ? "compact" : "comfortable")
                                      + "\ntoolbarSeparators=" + juce::String (toolbarLayout.separators ? 1 : 0)
                                      + "\ntoolbarVisible=" + juce::String (toolbarLayout.visible ? 1 : 0)
                                      + "\nautosaveMinutes=" + juce::String (autosaveMinutes) + "\nautosaveKeep=" + juce::String (autosaveKeep)
                                      + "\naskUnusedRecordings=" + juce::String (askUnusedRecordings ? 1 : 0)
                                      + "\nbackupWaitsForQuiet=" + juce::String (backupWaitsForQuiet ? 1 : 0)
                                      + "\nbackupLongestWait=" + juce::String (backupLongestWaitMinutes)
                                      + "\nhoverValues=" + juce::String (hoverValues ? 1 : 0)
                                      + "\nundoLevels=" + juce::String (engine.getUndoLevels())
                                      + "\nmetronomeVolume=" + juce::String (engine.getTransport().metronomeVolume.load(), 2)
                                      + "\nmetronomeSoundFl=" + juce::String (metronome::clampSound (engine.getTransport().metronomeSound.load()))
                                      + "\nmetronomeTrack=" + juce::String (engine.getTransport().metronomeTrack.load())
                                      + "\nplayTruncatedOnTransport=" + juce::String (engine.getTransport().playTruncatedOnTransport.load() ? 1 : 0)
                                      + "\nresetPluginsOnTransport=" + juce::String (engine.getTransport().resetPluginsOnTransport.load() ? 1 : 0)
                                      + "\npreviewVolume=" + juce::String (engine.getPreviewVolume(), 2)
                                      + "\neditorVolume=" + juce::String (engine.getEditorVolume(), 2)
                                      + "\nrackPianoRollView=" + juce::String (rack.isPianoRollView() ? 1 : 0)
                                      + "\naskWhatToRecord=" + juce::String (engine.getRecordOptions().askWhatToRecord ? 1 : 0)
                                      + "\nrecordNotes=" + juce::String (engine.getRecordOptions().recordNotes ? 1 : 0)
                                      + "\nrecordAudio=" + juce::String (engine.getRecordOptions().recordAudio ? 1 : 0)
                                      + "\nrecordStartsPlayback=" + juce::String (engine.getRecordOptions().startsPlayback ? 1 : 0)
                                      + "\ndisarmOnStop=" + juce::String (engine.getRecordOptions().disarmOnStop ? 1 : 0)
                                      + "\nrecordOffOnStop=" + juce::String (engine.getRecordOptions().recordOffOnStop ? 1 : 0)
                                      + "\nunarmAfterRender=" + juce::String (engine.getRecordOptions().unarmAfterRender ? 1 : 0)
                                      + "\nrenderAutoClip=" + juce::String (engine.getRecordOptions().autoCreateClip ? 1 : 0)
                                      + "\nrecordQuantiseStarts=" + juce::String (recordQuantiseOn ? 1 : 0)
                                      + "\nrecordNoteEnd=" + juce::String ((int) engine.getRecordOptions().noteEnd)
                                      + "\nrecordToStepSequencer=" + juce::String (engine.getRecordOptions().toStepSequencer ? 1 : 0)
                                      + "\nrecordingMarkers=" + juce::String (engine.getRecordOptions().recordingMarkers ? 1 : 0)
                                      + "\nrecordAutomation=" + juce::String (engine.getRecordOptions().recordAutomation ? 1 : 0)
                                      + "\nrecordClips=" + juce::String (engine.getRecordOptions().recordClips ? 1 : 0)
                                      + "\nrecordQuantiseAutomation=" + juce::String (engine.getRecordOptions().quantiseAutomation ? 1 : 0)
                                      + "\nrecordPunchLoop=" + juce::String (engine.getRecordOptions().punchLoopRegion ? 1 : 0)
                                      + "\nrecordLoop=" + juce::String (engine.getRecordOptions().loopRecord ? 1 : 0)
                                      + "\nrecordPreRoll=" + juce::String (engine.getRecordOptions().preRollBars)
                                      + "\nrecordCatchWindow=" + juce::String (engine.getRecordOptions().catchWindow)
                                      + "\nrecordCompensateOutput=" + juce::String (engine.getRecordOptions().compensateOutput ? 1 : 0)
                                      + "\nrecordPostRoll=" + juce::String (engine.getRecordOptions().postRollBars) + "\nmixerUsedOnly=" + juce::String (MixerStrips::showUsedOnly ? 1 : 0) + "\nmixerLayout=" + juce::String ((int) MixerStrips::layout)
                                      + "\nquantizeSongJumps=" + juce::String (engine.quantizeSongJumps.load()) + "\n"
                                      + juce::String (newProjectDefaults().toText()));
}

// --- transport -----------------------------------------------------------------------------------

void MainComponent::playStop()
{
    auto& transport = engine.getTransport();

    if (engine.isWaitingForInput() || engine.isCountingIn()) // pressed again: cancel
    {
        engine.stop();
        return;
    }

    if (transport.playing.load())
    {
        engine.stop();
        engine.seek (playStartTicks); // back to where playback started
    }
    else
    {
        playStartTicks = transport.positionTicks.load();
        engine.startPlayback();
    }
}

void MainComponent::playPause()
{
    if (engine.getTransport().playing.load() || engine.isWaitingForInput() || engine.isCountingIn())
        engine.stop();
    else
    {
        playStartTicks = engine.getTransport().positionTicks.load();
        engine.startPlayback();
    }
}

void MainComponent::playFromStart()
{
    // FL 2026's Shift+Space (measured in FL, issue #11): playing, recording, waiting or counting in -> stop at the start
    // of the time selection (Song mode) or else of the song / pattern; stopped -> play from the current position, as
    // Space does (a later Space goes back there).
    const auto wasPlaying = engine.getTransport().playing.load() || engine.isWaitingForInput() || engine.isCountingIn();
    if (! wasPlaying)
    {
        playStop();
        return;
    }

    engine.stop();
    const auto& song = engine.getSong();
    const auto target = transportedit::shiftSpaceStopTarget (song.mode == PlayMode::song, song.hasLoop(), song.loopStart);
    if (target > 0)
        engine.seek ((double) target);
    else
        engine.rewind();
    playStartTicks = (double) target;
}

void MainComponent::stopAndRewind (bool toStart)
{
    // Stop while nothing plays (the second press, or a double click) goes back to the very start even when
    // "Remember seek time" sends the first Stop to the ruler mark (round 162, issue #9)
    const auto alreadyStopped = toStart || ! (engine.getTransport().playing.load() || engine.isWaitingForInput() || engine.isCountingIn());
    engine.stop();
    // FL: the transport's Stop also ends a Browser sample preview (issue #11)
    browser.stopPreview(); // (the engine's one preview voice: the Sample browser window's preview stops too)
    const auto& globals = EditorGlobals::get();
    const auto back = transportedit::stopTarget (alreadyStopped, globals.rememberSeek && engine.getSong().mode == PlayMode::song, globals.seekMark);

    if (back > 0) // "Remember seek time": back to the position clicked on the Playlist ruler
        engine.seek ((double) back);
    else
        engine.rewind();

    playStartTicks = (double) back;
}

void MainComponent::showStopMenu()
{
    auto& globals = EditorGlobals::get();
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Stop"));
    menu.addItem (tr ("Remember seek time: Stop goes back to the position clicked on the Playlist ruler"), true, globals.rememberSeek, [this] {
        EditorGlobals::get().rememberSeek = ! EditorGlobals::get().rememberSeek;
        saveUiSettings();
    });
    menu.addSeparator();
    menu.addItem (tr ("All notes off (panic)") + "\t" + tr ("double-click"), [this] { engine.panic(); });
    menu.addItem (tr ("Back to the start"), [this] { stopAndRewind (true); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stopButton));
}

void MainComponent::toggleMode()
{
    engine.editSong ([] (Song& s) { s.mode = s.mode == PlayMode::song ? PlayMode::pattern : PlayMode::song; }, false);
}

// --- panels --------------------------------------------------------------------------------------

void MainComponent::togglePanel (Panel panel)
{
    workspace.toggle (panel);
    refreshPanelButtons();
}

void MainComponent::fitChannelRack (bool width)
{
    rack.updateSize();
    const auto scrollbar = rackViewport.getScrollBarThickness();
    // compact like FL's rack: at most two bars of steps are shown, longer patterns scroll sideways, so the
    // rack does not cover the Playlist after a project with long patterns is opened
    // and never past the desktop's right edge from where the rack is (the default layout keeps the Playlist's track
    // names uncovered on a small desktop, round 167v)
    const auto maxWidth = juce::jmin (juce::jmax (300, workspace.getWidth() - 16 - workspace.getPanelBounds (Panel::channelRack).getX() - scrollbar - 8), rack.widthForSteps (32));
    const auto contentWidth = juce::jmin (rack.naturalWidth(), maxWidth); // the ghost steps only fill a wider rack
    const auto needsHorizontal = rack.naturalWidth() > contentWidth;
    workspace.fitPanelContent (Panel::channelRack, width ? contentWidth + scrollbar : 0, rack.getHeight() + (needsHorizontal ? scrollbar : 0));
    fittedChannelCount = (int) engine.getSong().channels.size();
}

void MainComponent::showPanel (Panel panel)
{
    workspace.show (panel);
    refreshPanelButtons();
}

void MainComponent::showWindowsTip()
{
    const auto counter = PluginHost::settingsFolder().getChildFile ("windows-tip.txt");
    const auto shown = counter.existsAsFile() ? counter.loadFileAsString().trim().getIntValue() : 0;
    if (shown >= 3)
        return;
    counter.replaceWithText (juce::String (shown + 1));

    auto key = [this] (Panel p) {
        const auto chords = keyMap.chordsFor (panelAction (p));
        return chords.empty() ? juce::String() : "[" + juce::String (keys::toString (chords.front())) + "] ";
    };
    ui::Hint hint;
    hint.text = tr ("Windows: {0}Playlist  {1}Channel Rack  {2}Piano Roll  {3}Mixer  {4}Browser  (or the window buttons in the toolbar; again: hide)",
                    { key (Panel::playlist), key (Panel::channelRack), key (Panel::pianoRoll), key (Panel::mixer), key (Panel::browser) });
    ui::postHint (hint, 15000);
}

void MainComponent::refreshPanelButtons()
{
    for (int i = 0; i < workspace::panelCount; ++i)
        panelButtons[(std::size_t) i].setToggleState (workspace.isPanelVisible ((Panel) i), juce::dontSendNotification);
}

void MainComponent::openPianoRoll()
{
    showPanel (Panel::pianoRoll);
    pianoRoll.scrollToNotes();
}

void MainComponent::saveLayoutAs()
{
    auto* window = new juce::AlertWindow (tr ("Save layout as"), tr ("Name of the layout:"), juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", tr ("My layout"));
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim();
                                 if (result == 1 && name.isNotEmpty())
                                     workspace.saveLayoutAs (name);
                             }),
                             true);
    keepAboveMainWindow (*window);
}

juce::PopupMenu MainComponent::layoutsMenu()
{
    juce::PopupMenu menu;
    auto files = Workspace::layoutsFolder().findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();

    for (const auto& file : files)
        menu.addItem (file.getFileNameWithoutExtension(), [this, file] {
            workspace.loadLayout (file);
            refreshPanelButtons();
        });

    if (files.isEmpty())
        menu.addItem (tr ("(none saved yet)"), false, false, [] {});

    menu.addSeparator();
    menu.addItem (tr ("Open layouts folder"), [] {
        Workspace::layoutsFolder().createDirectory();
        Workspace::layoutsFolder().startAsProcess();
    });
    return menu;
}

void MainComponent::selectTabForSnapshot (int tab)
{
    const Panel panels[] = { Panel::channelRack, Panel::pianoRoll, Panel::playlist, Panel::mixer };
    const auto panel = panels[juce::jlimit (0, 3, tab)];

    if (panel == Panel::pianoRoll)
    {
        const auto& song = engine.getSong();
        std::vector<int> counts (song.channels.size(), 0);

        for (const auto& n : song.patterns[(std::size_t) song.currentPattern].notes)
            if (n.channel >= 0 && n.channel < (int) counts.size())
                ++counts[(std::size_t) n.channel];

        if (! counts.empty())
            engine.setSelectedChannel ((int) (std::max_element (counts.begin(), counts.end()) - counts.begin()));
    }

    // each view on its own: the panel maximized over the workspace
    workspace.resetLayout();
    workspace.show (panel);
    workspace.toggleMaximised (panel);
    refreshPanelButtons();

    if (panel == Panel::pianoRoll)
        pianoRoll.scrollToNotes();

    if (panel == Panel::playlist)
        playlist.resized(); // update the scrollbars for the loaded song

    if (panel == Panel::mixer)
        mixer.selectFirstTrackWithEffects();
}

void MainComponent::showDesktopForSnapshot (bool extraPanels)
{
    workspace.resetLayout();
    if (auto* stage = dynamic_cast<DanceStage*> (danceStage.get()))
        stage->update(); // (the project's dancers now: no timer runs before the picture)

    if (extraPanels)
    {
        workspace.show (Panel::mixer);
        workspace.show (Panel::pianoRoll);
        pianoRoll.scrollToNotes();
        workspace.show (Panel::channelRack);
    }

    refreshPanelButtons();
}

void MainComponent::showPanelsForSnapshot (std::initializer_list<Panel> panels)
{
    workspace.resetLayout();
    for (const auto panel : panels)
        workspace.show (panel);
    refreshPanelButtons();
}

// --- patterns ------------------------------------------------------------------------------------

void MainComponent::selectPattern (int index)
{
    if (index >= 0 && index < (int) engine.getSong().patterns.size() && index != engine.getSong().currentPattern)
        engine.editSong ([index] (Song& s) { s.currentPattern = index; }, false);
}

void MainComponent::stepPatternFromSelector (int delta)
{
    // FL's selector goes on past the last pattern (rack ops stepPatternCreating: one new empty pattern at most)
    const auto& song = engine.getSong();
    const auto last = (int) song.patterns.size() - 1;
    if (delta > 0 && last >= 0 && song.currentPattern + delta > last && ! isPatternEmpty (song.patterns[(std::size_t) last]))
        engine.editSong ([delta] (Song& s) { s.currentPattern = stepPatternCreating (s, delta); });
    else
        selectPattern (stepPattern (song, delta));
}

void MainComponent::nextEmptyPattern()
{
    engine.editSong ([] (Song& s) { gotoNextEmptyPattern (s); });
    showRenamePattern (engine.getSong().currentPattern);
}

void MainComponent::deleteCurrentPattern()
{
    // FL's Patterns › Delete (Ctrl+Shift+Del); the last pattern stays
    if (engine.getSong().patterns.size() > 1)
        engine.editSong ([] (Song& s) { s.removePattern (s.currentPattern); });
}

void MainComponent::insertPatternHere()
{
    engine.editSong ([] (Song& s) { insertPattern (s, s.currentPattern, newPatternName (s)); });
}

// --- window menus ------------------------------------------------------------------------------

// Each window's own items, at the top of the menu in its title bar (FL keeps an editor's options in the
// small menu at the left of its title bar). Items run the key map's actions, so they show their keys.
void MainComponent::addPanelMenuItems (Panel panel, juce::PopupMenu& menu)
{
    auto action = [this] (juce::PopupMenu& m, const char* id, const juce::String& text, bool enabled = true, bool ticked = false) {
        m.addItem (text + shortcutText (id), enabled, ticked, [this, id] { runAction (id); });
    };
    auto zoomMenu = [&action] (const juce::String& prefix) {
        juce::PopupMenu zoom;
        const std::pair<const char*, const char*> items[] = {
            { "zoomIn", TR_NOOP ("Zoom in") }, { "zoomOut", TR_NOOP ("Zoom out") }, { "zoom1", TR_NOOP ("Zoom: one bar") },
            { "zoom2", TR_NOOP ("Zoom: four bars") }, { "zoom3", TR_NOOP ("Zoom: sixteen bars") },
            { "zoomAll", TR_NOOP ("Zoom to show everything") }, { "zoomSelection", TR_NOOP ("Zoom to the selection") },
            { "centrePlayhead", TR_NOOP ("Centre the view on the playhead") }
        };
        for (const auto& [id, text] : items)
            if (const auto* a = keys::findAction ((prefix + id).toStdString())) // the key map holds the id strings
                action (zoom, a->id, tr (text));
        return zoom;
    };

    switch (panel)
    {
        case Panel::channelRack:
        {
            menu.addSubMenu (tr ("Pattern"), makePatternMenu());
            menu.addSeparator();
            action (menu, "rack.graphEditor", tr ("Graph lanes (velocity, pan, pitch...)"), true, rack.areGraphLanesShown());
            action (menu, "rack.pianoRollView", tr ("Piano-roll view of all channels"), true, rack.isPianoRollView());
            action (menu, "rack.rotateLeft", tr ("Rotate the selected channels' steps left"));
            action (menu, "rack.rotateRight", tr ("Rotate the selected channels' steps right"));
            juce::PopupMenu sort;
            sort.addItem (tr ("By name"), [this] { rack.sortChannels (ChannelSort::name); });
            sort.addItem (tr ("By color"), [this] { rack.sortChannels (ChannelSort::colour); });
            sort.addItem (tr ("By mixer insert"), [this] { rack.sortChannels (ChannelSort::insert); });
            sort.addItem (tr ("By type"), [this] { rack.sortChannels (ChannelSort::type); });
            menu.addSubMenu (tr ("Sort channels"), sort);
            menu.addItem (tr ("Select unused channels"), [this] { rack.selectUnused(); });
            action (menu, "rack.groupSelected", tr ("Group selected..."));
            menu.addSectionHeader (tr ("Options")); // (FL's SSMenu: "Mute removed steps" is in its Options part)
            menu.addItem (tr ("Mute removed steps"), true, ChannelRackComponent::muteRemovedSteps, [this] {
                ChannelRackComponent::muteRemovedSteps = ! ChannelRackComponent::muteRemovedSteps;
                saveUiSettings();
            });
            break;
        }
        case Panel::playlist:
            menu.addItem (tr ("Maximize or restore the Playlist") + shortcutText ("panel.maximisePlaylist"), [this] {
                workspace.toggleMaximised (Panel::playlist);
                refreshPanelButtons();
            });
            menu.addSeparator();
            action (menu, "playlist.ripple", tr ("Ripple editing (resizing and deleting move the later clips)"), true, playlist.isRippleOn());
            action (menu, "playlist.rippleDelete", tr ("Delete the selected clips and close the gaps"));
            action (menu, "playlist.autoCrossfades", tr ("Automatic crossfades (current project)"), true, engine.getSong().autoCrossfades);
            // FL's legacy Pattern Blocks (round 170f, issue #60): FL 10's "Enable legacy pattern blocks", per project here
            action (menu, "playlist.patternBlocks", tr ("Pattern blocks (current project)"), true, engine.getSong().patternBlocksOn);
            action (menu, "playlist.blocksToClips", tr ("Turn pattern blocks into clips"), ! engine.getSong().blocks.empty());
            action (menu, "playlist.follow", tr ("Follow the playhead (auto scrolling)"), true, EditorGlobals::get().isFollowing (false));
            action (menu, "playlist.pitchClipsDown", tr ("Pitch selected audio clips one semitone down"));
            action (menu, "playlist.pitchClipsUp", tr ("Pitch selected audio clips one semitone up"));
            action (menu, "playlist.reverseClips", tr ("Reverse selected audio clips"));
            action (menu, "playlist.quantizeStarts", tr ("Quantize the clips' starts"));
            action (menu, "playlist.quantizeHalf", tr ("Quantize the clips' starts 50% (halfway to the grid)"));
            // round 168f: FL's Playlist Edit / Tools items ([DFM] EEMenu)
            action (menu, "playlist.muteClips", tr ("Mute"));     // FL's Alt+M / Shift+Alt+M (issue #53)
            action (menu, "playlist.unmuteClips", tr ("Unmute"));
            action (menu, "playlist.makeUnique", tr ("Make unique"));
            action (menu, "playlist.mergePatternClips", tr ("Merge pattern clips"));
            action (menu, "playlist.purgeUnusedAudio", tr ("Purge unused audio clips"));
            menu.addSubMenu (tr ("Picker panel"), picker.pickerMenu()); // (round 177d: FL's Playlist menu › Picker panel)
            action (menu, "playlist.stretchMode", tr ("Stretch audio when resizing audio clips"), true, EditorGlobals::get().stretchOnResize);
            action (menu, "playlist.createVariants", tr ("Create variants when stretching audio clips"), true, EditorGlobals::get().createVariants);
            {
                juce::PopupMenu trackTools;
                action (trackTools, "playlist.cloneTracks", tr ("Clone selected playlist track(s)"));
                action (trackTools, "playlist.lockAllToContent", tr ("Lock all playlist tracks to content"));
                action (trackTools, "playlist.unlockAllFromContent", tr ("Unlock all playlist tracks from content"));
                action (trackTools, "playlist.resetEmptyTracks", tr ("Reset empty playlist tracks"));
                action (trackTools, "playlist.collapseAllGroups", tr ("Collapse all grouped tracks"));
                action (trackTools, "playlist.expandAllGroups", tr ("Expand all grouped tracks"));
                menu.addSubMenu (tr ("Tools"), trackTools);
            }
            {
                // FL's Playlist Tools › EERenderPLSelectionMenu, EEPLRenderAllTracksMenu, EEPLRenderAllTrackGroupsMenu (round 168e)
                juce::PopupMenu consolidate;
                action (consolidate, "tools.consolidateSelection", tr ("From selection start"));
                action (consolidate, "tools.consolidateSelectionSongStart", tr ("From song start"));
                menu.addSubMenu (tr ("Consolidate playlist selection"), consolidate);
                menu.addSubMenu (tr ("Export all playlist tracks"), playlistTrackExportMenu (false));
                menu.addSubMenu (tr ("Export all playlist track groups"), playlistTrackExportMenu (true));
            }
            menu.addSubMenu (tr ("Zoom"), zoomMenu ("playlist."));
            menu.addSeparator();
            // FL's Performance mode (round 165m)
            action (menu, "playlist.performanceMode", tr ("Performance mode"), true, engine.getSong().performanceMode);
            action (menu, "playlist.zoomPerformance", tr ("Zoom on performance clips"), engine.getSong().performanceZoneEnd() > 0);
            action (menu, "playlist.perfClipProgress", tr ("Performance clip progress"), true, playlist.clipProgressShown());
            action (menu, "playlist.perfTrackProgress", tr ("Performance track progress"), true, playlist.trackProgressShown());
            break;
        case Panel::pianoRoll:
        {
            juce::PopupMenu tools;
            for (const auto* id : { "pianoRoll.riffMachine", "pianoRoll.chordProgression", "pianoRoll.articulate", "pianoRoll.quantize",
                                    "pianoRoll.chop", "pianoRoll.arpeggiate", "pianoRoll.strum", "pianoRoll.flam", "pianoRoll.claw",
                                    "pianoRoll.limit", "pianoRoll.flip", "pianoRoll.randomise", "pianoRoll.scaleLevels", "pianoRoll.lfo" })
                if (const auto* a = keys::findAction (id))
                    action (tools, a->id, tr (a->description));
            menu.addSubMenu (tr ("Tools"), tools);
            action (menu, "pianoRoll.quickQuantize", tr ("Quick quantize (starts and lengths)"));
            action (menu, "pianoRoll.quantizeStarts", tr ("Quantize starts"));
            action (menu, "pianoRoll.quantizeHalf", tr ("Quantize starts 50% (halfway to the grid)"));
            action (menu, "pianoRoll.follow", tr ("Follow the playhead (auto scrolling)"), true, EditorGlobals::get().isFollowing (true));
            menu.addSubMenu (tr ("Zoom"), zoomMenu ("pianoRoll."));
            menu.addItem (tr ("Snap notes to the scale"), pianoRoll.hasScale(), pianoRoll.isScaleSnapOn(),
                          [this] { pianoRoll.setScaleSnap (! pianoRoll.isScaleSnapOn()); });
            break;
        }
        case Panel::mixer:
            action (menu, "mixer.moveLeft", tr ("Move the selected inserts left"));
            action (menu, "mixer.moveRight", tr ("Move the selected inserts right"));
            menu.addSeparator();
            action (menu, "mixer.linkChannels", tr ("Link the selected channels to the selected insert"));
            action (menu, "mixer.linkChannelsFrom", tr ("Link the selected channels to inserts starting from the selected one"));
            action (menu, "mixer.selectLinkedChannels", tr ("Select the channels linked to the selected insert"));
            break;
        case Panel::browser:
            action (menu, "browser.search", tr ("Search the Browser"));
            browser.addViewItems (menu); // what it lists, which folders it shows (round 163)
            menu.addItem (tr ("Add folder..."), [this] { browser.addUserFolder(); });
            action (menu, "view.sampleBrowser", tr ("Sample browser (separate window)"));
            menu.addSeparator();
            // the search index (round 162, issue #9): searches read it; it follows changed folders by itself
            menu.addSectionHeader (browser.searchIndexStatus());
            menu.addItem (tr ("Refresh search index"), [this] { browser.refreshSearchIndex (false); });
            menu.addItem (tr ("Rebuild search index"), [this] { browser.refreshSearchIndex (true); });
            break;
        default:
            break;
    }
}

// --- quick-add palette and history -------------------------------------------------------------

void MainComponent::togglePluginPicker()
{
    // FL's ShowPluginPickerAction (F8): opens the picker over the main window, or closes it (REA_rodada169k §2.5)
    if (pluginPickerView != nullptr)
    {
        auto old = std::move (pluginPickerView);
        juce::MessageManager::callAsync ([keep = std::shared_ptr<juce::Component> (std::move (old))] {}); // (it may be on the stack)
        return;
    }
    PluginPickerTargets targets;
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    targets.addInstrument = [safe] (ChannelDef c) {
        if (safe == nullptr)
            return;
        safe->rack.addChannel (std::move (c), true);
        safe->showPanel (Panel::channelRack);
    };
    targets.addEffect = [safe] (EffectSlot slot) {
        if (safe == nullptr)
            return;
        if (! safe->mixer.addEffectToSelected (std::move (slot)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin picker"), tr ("The selected insert has no free effect slot."));
        else
        {
            safe->showPanel (Panel::mixer);
            // FL opens the new effect's window (round 171c, its "insert effect plugin")
            const auto track = safe->mixer.getSelectedTrack();
            if (track >= 0 && track < (int) safe->engine.getSong().mixer.size())
                safe->mixer.openSlotEditor (track, (int) safe->engine.getSong().mixer[(std::size_t) track].effects.size() - 1);
        }
    };
    targets.close = [safe] {
        if (safe != nullptr && safe->pluginPickerView != nullptr)
            safe->togglePluginPicker();
    };
    targets.showMixer = [safe] { // (an effect dragged out of the picker: FL brings up the Mixer)
        if (safe != nullptr)
            safe->showPanel (Panel::mixer);
    };
    targets.openBrowser = [safe] (const std::string& category, bool generators) {
        if (safe == nullptr)
            return;
        safe->showPanel (Panel::browser);
        safe->browser.showPluginDatabase (category.empty() ? juce::String() : tr (category.c_str()), category.empty() ? -1 : (generators ? 1 : 0));
    };
    targets.dragOwner = [safe]() -> juce::Component* { return safe.getComponent(); };
    pluginPickerView = makePluginPickerView (engine, std::move (targets));
    // FL's picker form covers the work area of the main window's monitor, whatever the main window's size (round 175b:
    // FL at 1280 × 720 in a 1920 screen still opens it over the whole screen), and lets the screen show through
    const auto* top = getTopLevelComponent();
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (top != nullptr ? top->getScreenBounds() : getScreenBounds());
    const auto area = display != nullptr ? display->userArea : getScreenBounds();
    takePluginPickerBackdrop (*pluginPickerView, area);
    pluginPickerView->setBounds (area);
    pluginPickerView->addToDesktop (0); // (no frame, not on the taskbar)
    pluginPickerView->setVisible (true);
    pluginPickerView->toFront (true);
    pluginPickerView->grabKeyboardFocus();
}

void MainComponent::showQuickAdd()
{
    QuickAddTargets targets;
    targets.effectsFirst = workspace.getFocused() == Panel::mixer;
    targets.addInstrument = [this] (ChannelDef c) {
        rack.addChannel (std::move (c), true);
        showPanel (Panel::channelRack);
    };
    targets.addEffect = [this] (EffectSlot slot) {
        if (! mixer.addEffectToSelected (std::move (slot)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Quick-add palette"),
                                                    tr ("The selected insert has no free effect slot."));
        else
            showPanel (Panel::mixer);
    };
    openToolWindow (makeQuickAddPalette (engine, std::move (targets)), tr ("Quick-add palette"), "quickadd");
}

std::unique_ptr<juce::Component> MainComponent::makeQuickAddForSnapshot()
{
    QuickAddTargets targets;
    targets.addInstrument = [] (ChannelDef) {};
    targets.addEffect = [] (EffectSlot) {};
    return makeQuickAddPalette (engine, std::move (targets));
}

void MainComponent::showHistory()
{
    if (auto* open = findToolWindow ("history"))
    {
        open->toFront (true);
        return;
    }

    openToolWindow (makeHistoryPanel (engine), tr ("Undo history"), "history");
}

// --- recording options ---------------------------------------------------------------------------

void MainComponent::updateRecordQuantise()
{
    // the global grid sets the record quantise; "none" and the zoom-dependent grids record as played
    std::int64_t ticks = 0;

    // (the grid is kept even with "Note start time" off: "Note end time" uses it too)
    {
        const auto& option = grid::options()[(std::size_t) juce::jlimit (0, (int) grid::options().size() - 1, EditorGlobals::get().getGridOption())];
        if (option.kind == grid::Kind::ticks)
            ticks = option.ticks;
        else if (option.kind == grid::Kind::bar)
            ticks = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    }

    if (engine.getRecordOptions().quantise != ticks || engine.getRecordOptions().quantiseStarts != recordQuantiseOn)
    {
        auto o = engine.getRecordOptions();
        o.quantise = ticks;
        o.quantiseStarts = recordQuantiseOn;
        engine.setRecordOptions (o);
    }
}

void MainComponent::addRecordQuantizeItems (juce::PopupMenu& menu)
{
    const auto o = engine.getRecordOptions();
    auto set = [this] (std::function<void (AudioEngine::RecordOptions&)> change) {
        auto options = engine.getRecordOptions();
        change (options);
        engine.setRecordOptions (options);
        saveUiSettings();
    };
    // FL's record menu "Quantize" items (REA round 139), to the global snap
    menu.addItem (tr ("Note start time"), ! o.toStepSequencer, recordQuantiseOn && ! o.toStepSequencer, [this] {
        recordQuantiseOn = ! recordQuantiseOn;
        updateRecordQuantise();
        saveUiSettings();
    });
    menu.addItem (tr ("Note end time"), ! o.toStepSequencer, o.noteEnd == NoteEnd::quantize && ! o.toStepSequencer, [set] {
        set ([] (AudioEngine::RecordOptions& r) { r.noteEnd = r.noteEnd == NoteEnd::quantize ? NoteEnd::keepEnd : NoteEnd::quantize; });
    });
    menu.addItem (tr ("Leave note duration"), ! o.toStepSequencer, o.noteEnd == NoteEnd::keepLength && ! o.toStepSequencer, [set] {
        set ([] (AudioEngine::RecordOptions& r) { r.noteEnd = r.noteEnd == NoteEnd::keepLength ? NoteEnd::keepEnd : NoteEnd::keepLength; });
    });
    menu.addItem (tr ("Record to step sequencer"), true, o.toStepSequencer,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.toStepSequencer = ! r.toStepSequencer; }); });
    // FL's "Automations" (RecQuantizeAutomationsMenu, setting QuantizeAutomations; REA round 143): recorded automation
    // points go to the snap
    menu.addItem (tr ("Automations"), true, o.quantiseAutomation,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.quantiseAutomation = ! r.quantiseAutomation; }); });
}

void MainComponent::addRecordOptions (juce::PopupMenu& menu)
{
    const auto o = engine.getRecordOptions();
    auto set = [this] (std::function<void (AudioEngine::RecordOptions&)> change) {
        auto options = engine.getRecordOptions();
        change (options);
        engine.setRecordOptions (options);
        refreshRecordOptionButtons();
        saveUiSettings();
    };

    juce::PopupMenu countIn;
    countIn.addItem (tr ("Off"), true, o.countInBars == 0, [set] { set ([] (AudioEngine::RecordOptions& r) { r.countInBars = 0; }); });
    for (int bars : { 1, 2, 4 })
        countIn.addItem (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }), true, o.countInBars == bars,
                         [set, bars] { set ([bars] (AudioEngine::RecordOptions& r) { r.countInBars = bars; }); });
    menu.addSubMenu (tr ("Count-in") + shortcutText ("record.countIn"), countIn);
    menu.addItem (tr ("Wait for the first note to start") + shortcutText ("record.waitForInput"), true, o.waitForInput,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.waitForInput = ! r.waitForInput; }); });
    menu.addItem (tr ("Loop record") + shortcutText ("record.loop"), true, o.loopRecord,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.loopRecord = ! r.loopRecord; }); });
    menu.addItem (tr ("Step edit") + shortcutText ("record.stepEdit"), true, engine.isStepEdit(), [this] { runAction ("record.stepEdit"); });
    // round 170d: notes played during the count-in are kept; the ones just before the start go onto it
    {
        juce::PopupMenu catchMenu;
        catchMenu.addItem (tr ("Off (keep their real time)"), true, o.catchWindow == 0,
                           [set] { set ([] (AudioEngine::RecordOptions& r) { r.catchWindow = 0; }); });
        const std::pair<std::int64_t, const char*> windows[] = { { ppq / 8, TR_NOOP ("1/32 note") },
                                                                 { ppq / 4, TR_NOOP ("1/16 note") },
                                                                 { ppq / 2, TR_NOOP ("1/8 note") },
                                                                 { ppq, TR_NOOP ("1/4 note") } };
        for (const auto& [ticks, name] : windows)
            catchMenu.addItem (tr (name), true, o.catchWindow == ticks,
                               [set, ticks = ticks] { set ([ticks] (AudioEngine::RecordOptions& r) { r.catchWindow = ticks; }); });
        menu.addSubMenu (tr ("Notes played just before the start go onto it"), catchMenu);
    }
    menu.addItem (tr ("Played notes: compensate the output latency"), true, o.compensateOutput,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.compensateOutput = ! r.compensateOutput; }); });
    menu.addItem (tr ("Capture MIDI: the notes just played without recording into the pattern") + shortcutText ("record.captureMidi"),
                  [this] { runAction ("record.captureMidi"); });
    // round 163: punch in/out on the loop region, pre-roll and post-roll (bars)
    {
        auto bars = [&set] (juce::PopupMenu& m, int now, std::function<void (AudioEngine::RecordOptions&, int)> apply) {
            m.addItem (tr ("Off"), true, now == 0, [set, apply] { set ([apply] (AudioEngine::RecordOptions& r) { apply (r, 0); }); });
            for (int n : { 1, 2, 4 })
                m.addItem (n == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (n) }), true, now == n,
                           [set, apply, n] { set ([apply, n] (AudioEngine::RecordOptions& r) { apply (r, n); }); });
        };
        juce::PopupMenu pre, post;
        bars (pre, o.preRollBars, [] (AudioEngine::RecordOptions& r, int n) { r.preRollBars = n; });
        bars (post, o.postRollBars, [] (AudioEngine::RecordOptions& r, int n) { r.postRollBars = n; });
        menu.addItem (tr ("Audio: punch in/out on the loop region (plays through it, keeps only the region)"), true, o.punchLoopRegion,
                      [set] { set ([] (AudioEngine::RecordOptions& r) { r.punchLoopRegion = ! r.punchLoopRegion; }); });
        menu.addSubMenu (tr ("Audio: pre-roll (plays before the punch-in or the start position)"), pre);
        menu.addSubMenu (tr ("Audio: post-roll (plays after the punch-out, then stops)"), post);
    }
    menu.addSeparator();
    menu.addItem (tr ("Overdub: add to the pattern"), true, o.mode == RecordMode::overdub,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.mode = RecordMode::overdub; }); });
    menu.addItem (tr ("Replace: a recorded channel's old notes are cleared"), true, o.mode == RecordMode::replace,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.mode = RecordMode::replace; }); });
    menu.addSeparator();
    {
        juce::PopupMenu quantize;
        addRecordQuantizeItems (quantize);
        menu.addSubMenu (tr ("Quantize recorded notes to the global snap"), quantize);
    }
    menu.addItem (tr ("Song mode: place a pattern clip where recording starts"), true, o.placeSongClip,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.placeSongClip = ! r.placeSongClip; }); });
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
    {
        engine.setRecording (false);
        return;
    }

    if (! engine.getRecordOptions().askWhatToRecord || ! persistent)
    {
        engine.setRecording (true);
        if (engine.getRecordOptions().startsPlayback && ! engine.getTransport().playing.load()) // FL's "Recording starts playback"
            engine.startPlayback();
        return;
    }

    juce::Component::SafePointer<MainComponent> safe (this);
    showRecordChoiceDialog ([safe] (int choice, bool dontAsk) {
        if (safe == nullptr)
            return;
        if (choice == recordChoiceAudioEditor)
        {
            openAudioEditorRecording (safe->engine);
            if (! safe->engine.getTransport().playing.load())
                safe->engine.play();
            return;
        }
        auto o = safe->engine.getRecordOptions();
        o.recordNotes = choice != recordChoiceAudio;
        o.recordAudio = choice != recordChoiceNotes;
        o.recordAutomation = choice != recordChoiceAudio;
        if (dontAsk)
            o.askWhatToRecord = false;
        safe->engine.setRecordOptions (o);
        if (dontAsk)
            safe->saveUiSettings();
        if (choice == recordChoiceAudio && safe->engine.getArmedInserts().empty())
            safe->armSelectedInsertForAudio();
        safe->engine.setRecording (true);
        if (o.startsPlayback && ! safe->engine.getTransport().playing.load()) // FL's "Recording starts playback"
            safe->engine.startPlayback();
    });
}

// Round 163: the fastest way from "press record" to a clip on the Playlist — one key arms the selected insert (or the
// one with an input), records audio only (no question) and starts playback (count-in / pre-roll as set); pressed
// again it stops, and the take is on the Playlist.
void MainComponent::quickRecordAudio()
{
    auto& transport = engine.getTransport();
    if (engine.isRecording() && (transport.playing.load() || engine.isCountingIn() || engine.isWaitingForInput()))
    {
        engine.stop();
        engine.setRecording (false);
        return;
    }
    if (engine.getArmedInserts().empty())
        armSelectedInsertForAudio();
    if (engine.getArmedInserts().empty()) // no input yet: its menu is open, nothing to record
        return;
    if (auto o = engine.getRecordOptions(); ! o.recordAudio)
    {
        o.recordAudio = true;
        engine.setRecordOptions (o);
    }
    engine.setRecording (true);
    if (! transport.playing.load())
        engine.startPlayback();
}

void MainComponent::armSelectedInsertForAudio()
{
    const auto& mixer = engine.getSong().mixer;

    // the selected insert, else the first one with an input, else a free one
    auto target = this->mixer.getStripsForSnapshot().getSelectedTrack();
    for (int t = 1; target <= 0 && t < (int) mixer.size(); ++t)
        if (mixer[(std::size_t) t].inputChannel >= 0)
            target = t;
    if (target <= 0)
        target = engine.getSong().nextFreeInsert();
    if (target <= 0 || target >= (int) mixer.size())
        return;

    showPanel (Panel::mixer);
    this->mixer.selectTrack (target);
    if (engine.isInsertArmed (target))
        return;
    // this menu item says "(choose its input)": without one, the input menu comes first and arms after the choice
    if (mixer[(std::size_t) target].inputChannel < 0)
        mixerinput::showMenu (engine, target, &this->mixer, [this, target] { engine.setInsertArmed (target, true); });
    else
        mixerinput::toggleArm (engine, target, &this->mixer);
}

void MainComponent::showMetronomeMenu()
{
    // FL 2026's right-click on the metronome button, as it is (REA round 165z-a2): the "Metronome sound" choice only,
    // in FL's order. The volume trim is in Options, the count-in has its own button and the track the click plays
    // through is in Settings › Audio › Mixer.
    auto& transport = engine.getTransport();
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Metronome sound"));
    for (int i = 0; i < metronome::soundCount; ++i)
        menu.addItem (tr (metronome::soundName (i)), true, metronome::clampSound (transport.metronomeSound.load()) == i, [this, i] {
            engine.getTransport().metronomeSound.store (i);
            saveUiSettings();
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metronomeButton));
}

void MainComponent::showTypingPianoMenu()
{
    // FL 2026's right-click on the typing keyboard button (KbToMIDIPopup, measured round 168t2): Root note C..B,
    // Octave 2..6 (where the Z row starts), Options › Reset, Layout (FL's .scr names and order, core/KeyMap.h).
    // FL's Reset (KbToMIDIResetClick 0x121ac70) releases the held notes and puts Root note C, Octave 4 and Layout Piano back.
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Root note"));
    for (int r = 0; r < 12; ++r)
        menu.addItem (names[r], true, typingRoot == r, [this, r] { typingRoot = r; saveUiSettings(); });
    menu.addSectionHeader (tr ("Octave"));
    for (int o = keys::typingpiano::minOctave; o <= keys::typingpiano::maxOctave; ++o)
        menu.addItem (juce::String (o), true, typingOctave == o, [this, o] { typingOctave = o; saveUiSettings(); });
    menu.addSectionHeader (tr ("Options"));
    menu.addItem (tr ("Reset"), [this] {
        heldKeys.releaseAll();
        releaseTypingLaunches (true);
        typingOctave = keys::typingpiano::defaultOctave;
        typingRoot = 0;
        typingLayout = keys::typingpiano::pianoLayout();
        saveUiSettings();
    });
    menu.addSectionHeader (tr ("Layout"));
    for (int l = 0; l < keys::typingpiano::layoutCount(); ++l) // (FL's file names, untranslated as in FL)
        menu.addItem (keys::typingpiano::layoutName (l), true, typingLayout == l, [this, l] {
            heldKeys.releaseAll(); // a held key's notes belong to the old layout
            releaseTypingLaunches (true);
            typingLayout = l;
            saveUiSettings();
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&typingPianoButton));
}

void MainComponent::dumpScoreLog (int minutes)
{
    const auto& song = engine.getSong();
    scorelog::DumpOptions options;
    options.minutes = minutes;
    options.bpm = song.bpm;
    options.beatsPerBar = song.beatsPerBar;
    auto notes = scorelog::dump (engine.scoreLogSnapshot(), options);
    // (channels deleted since the notes were played are left out)
    notes.erase (std::remove_if (notes.begin(), notes.end(), [&song] (const Note& n) { return n.channel < 0 || n.channel >= (int) song.channels.size(); }),
                 notes.end());
    if (notes.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Score logger"),
                                                tr ("The score log is empty: no notes were played on a MIDI keyboard or the typing keyboard in that time."));
        return;
    }

    auto apply = [this] (std::vector<Note> logged, bool replace) {
        const auto count = logged.size();
        engine.editSong ([&logged, replace] (Song& s) {
            if (s.patterns.empty())
                s.patterns.emplace_back();
            s.currentPattern = juce::jlimit (0, (int) s.patterns.size() - 1, s.currentPattern);
            auto& pattern = s.patterns[(std::size_t) s.currentPattern];
            if (replace)
                pattern.notes.clear();
            pattern.notes.insert (pattern.notes.end(), logged.begin(), logged.end());
            pattern.sortNotes();
        });
        ui::postHint ({ tr ("Score log: {0} notes put in the pattern", { juce::String ((int) count) }), {}, false, false }, 4000);
    };

    const auto& patterns = song.patterns;
    const auto current = song.currentPattern;
    if (current < 0 || current >= (int) patterns.size() || patterns[(std::size_t) current].notes.empty())
    {
        apply (std::move (notes), false);
        return;
    }

    auto box = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("Score logger"))
                       .withMessage (tr ("The current pattern isn't empty. Do you want the score log to be merged, or to replace the current content?"))
                       .withButton (tr ("Merge"))
                       .withButton (tr ("Replace"))
                       .withButton (tr ("Cancel"));
    juce::AlertWindow::showAsync (box, [apply, notes] (int result) mutable {
        if (result == 1 || result == 2)
            apply (std::move (notes), result == 2);
    });
}

void MainComponent::showMetronomeVolume()
{
    // a callout with the volume knob; the click plays at the new level at once (Transport::metronomeVolume)
    struct VolumePanel final : public juce::Component
    {
        VolumePanel (MainComponent& m) : owner (m)
        {
            title.setText (tr ("Metronome volume"), juce::dontSendNotification);
            title.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (title);
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 18);
            knob.setRange (0.0, controls::maxUtilityVolume, 0.01);
            knob.setDoubleClickReturnValue (true, 1.0);
            knob.textFromValueFunction = [] (double v) { return ui::utilityVolumeText (v); };
            knob.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; };
            knob.setValue (owner.engine.getTransport().metronomeVolume.load(), juce::dontSendNotification);
            knob.onValueChange = [this] { owner.engine.getTransport().metronomeVolume.store ((float) knob.getValue()); };
            knob.onDragEnd = [this] { owner.saveUiSettings(); };
            ui::setHint (knob, tr ("Metronome volume: 100% is the default click level, up to 125%  [2xLMB] 100%"));
            addAndMakeVisible (knob);
            setSize (170, 150);
        }
        ~VolumePanel() override { owner.saveUiSettings(); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            title.setBounds (r.removeFromTop (20));
            knob.setBounds (r);
        }
        MainComponent& owner;
        juce::Label title;
        juce::Slider knob;
    };
    juce::CallOutBox::launchAsynchronously (std::make_unique<VolumePanel> (*this), metronomeButton.getScreenBounds(), nullptr);
}

void MainComponent::openAudioEditorForSelection()
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();

    if (juce::isPositiveAndBelow (channel, (int) song.channels.size()))
    {
        const auto& c = song.channels[(std::size_t) channel];
        if ((c.instrument == InstrumentKind::sampler || c.instrument == InstrumentKind::audioClip) && ! c.samplePath.empty())
        {
            openAudioEditorForChannel (engine, channel);
            return;
        }
    }

    openAudioEditor (engine);
}

void MainComponent::showRecordMenu()
{
    const auto o = engine.getRecordOptions();
    auto set = [this] (std::function<void (AudioEngine::RecordOptions&)> change) {
        auto options = engine.getRecordOptions();
        change (options);
        engine.setRecordOptions (options);
        saveUiSettings();
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Recording filter"));
    // FL's filter order (RecMenu, REA round 143): Automation, Notes, Audio
    menu.addItem (tr ("Automation (the controls you move: automation clips in song mode, pattern events in pattern mode)"), true,
                  o.recordAutomation, [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordAutomation = ! r.recordAutomation; }); });
    menu.addItem (tr ("Notes (MIDI and PC keyboard) into the current pattern"), true, o.recordNotes,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordNotes = ! r.recordNotes; }); });
    menu.addItem (tr ("Audio of the armed mixer inserts (becomes Playlist clips)"), true, o.recordAudio,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordAudio = ! r.recordAudio; }); });
    menu.addItem (tr ("Clips (Performance mode: the launched clips are written into the arrangement)"), true, o.recordClips,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordClips = ! r.recordClips; }); });
    menu.addItem (tr ("Ask what to record when recording starts"), true, o.askWhatToRecord,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.askWhatToRecord = ! r.askWhatToRecord; }); });
    // FL's "Options" section (REA round 134)
    menu.addSectionHeader (tr ("Options"));
    menu.addItem (tr ("Recording starts playback"), true, o.startsPlayback,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.startsPlayback = ! r.startsPlayback; }); });
    menu.addItem (tr ("Disarm on stop (recording switches off when playback stops)"), true, o.recordOffOnStop,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordOffOnStop = ! r.recordOffOnStop; }); });
    // LYNX's own (issue #16; FL has no such option): also in Mixer › Disk recording
    menu.addItem (tr ("Disarm the inserts when recording stops"), true, o.disarmOnStop,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.disarmOnStop = ! r.disarmOnStop; }); });
    menu.addItem (tr ("Enable recording markers (Start / Stop recording markers in the Playlist)"), true, o.recordingMarkers,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.recordingMarkers = ! r.recordingMarkers; }); });
    // FL's "Quantize" section (REA round 139): to the global snap
    menu.addSectionHeader (tr ("Quantize"));
    addRecordQuantizeItems (menu);
    menu.addItem (tr ("Record into the audio editor..."), [this] { openAudioEditor (engine); });
    menu.addSeparator();
    menu.addItem (tr ("Arm the selected mixer insert for audio (choose its input)..."), [this] { armSelectedInsertForAudio(); });
    menu.addItem (tr ("Audio settings (input device)..."), [this] { openSettings ((int) SettingsPage::audio); });
    menu.addItem (tr ("Open recordings folder"), [this] {
        const auto folder = engine.getRecordingFolder();
        folder.createDirectory();
        folder.startAsProcess();
    });
    menu.addItem (tr ("Delete unused recordings and save..."), [this] { deleteUnusedRecordingsAndSave(); });
    menu.addSeparator();
    addRecordOptions (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (recordMenuButton.isVisible() ? (juce::Component*) &recordMenuButton
                                                                                                      : (juce::Component*) &recordButton));
}

void MainComponent::refreshRecordOptionButtons()
{
    auto withKey = [this] (const juce::String& text, const char* id) {
        const auto key = shortcutText (id).trim();
        return key.isEmpty() ? text : text + " (" + key + ")";
    };
    const auto o = engine.getRecordOptions();
    countInButton.setToggleState (o.countInBars > 0, juce::dontSendNotification);
    countInButton.setTooltip (withKey (o.countInBars > 0 ? tr ("Count-in before recording: {0} bar(s)", { juce::String (o.countInBars) })
                                                         : tr ("Count-in before recording: off"),
                                       "record.countIn"));
    blendButton.setToggleState (o.mode == RecordMode::overdub, juce::dontSendNotification);
    blendButton.setTooltip (o.mode == RecordMode::overdub ? tr ("Blend recorded notes: on (new notes join the pattern's notes)")
                                                          : tr ("Blend recorded notes: off (a recorded channel's old notes are replaced)"));
    waitInputButton.setToggleState (o.waitForInput, juce::dontSendNotification);
    waitInputButton.setTooltip (withKey (o.waitForInput ? tr ("Recording waits for the first note: on") : tr ("Recording waits for the first note: off"),
                                         "record.waitForInput"));
    blendButton.setTooltip (withKey (blendButton.getTooltip(), "record.blend"));
    loopRecordButton.setToggleState (o.loopRecord, juce::dontSendNotification);
    loopRecordButton.setTooltip (withKey (o.loopRecord ? tr ("Loop record: on (recording loops: the loop region in Song mode, one take per pass; the pattern in Pattern mode)")
                                                       : tr ("Loop record: off (recording plays straight on: through the loop region, or past the pattern's end, which grows)"),
                                          "record.loop"));
    stepEditButton.setToggleState (engine.isStepEdit(), juce::dontSendNotification);
    stepEditButton.setTooltip (withKey (engine.isStepEdit() ? tr ("Step edit: on (stopped, played notes go into the pattern one snap step after another)")
                                                            : tr ("Step edit: off"),
                                        "record.stepEdit"));
    punchButton.setToggleState (o.punchLoopRegion, juce::dontSendNotification);
    punchButton.setTooltip (withKey (o.punchLoopRegion ? tr ("Punch in/out: on (audio is kept only inside the loop region)") : tr ("Punch in/out: off"),
                                     "record.punch"));
}

void MainComponent::clonePatternNow()
{
    engine.editSong ([] (Song& s) { clonePattern (s, s.currentPattern); });
}

void MainComponent::movePatternNow (int delta)
{
    engine.editSongPatternOrder ([delta] (Song& s) { movePattern (s, s.currentPattern, delta); });
}

void MainComponent::splitPatternByChannelNow()
{
    int created = 0;
    engine.editSong ([&created] (Song& s) { created = splitPatternByChannel (s, s.currentPattern); });

    if (created == 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Split by channel"), tr ("This pattern has no notes."));
}

void MainComponent::showRenamePattern (int index)
{
    if (index < 0 || index >= (int) engine.getSong().patterns.size())
        return;

    const auto& p = engine.getSong().patterns[(std::size_t) index];
    showNameAndColourDialog (tr ("Rename pattern"), juce::String::fromUTF8 (p.name.c_str()), p.colour,
                             [this, index] (const juce::String& name, std::uint32_t colour) {
                                 if (index < (int) engine.getSong().patterns.size())
                                     engine.editSong ([index, n = name.toStdString(), colour] (Song& s) {
                                         s.patterns[(std::size_t) index].name = n;
                                         s.patterns[(std::size_t) index].colour = colour;
                                     });
                             });
}

void MainComponent::refreshToolbar()
{
    const auto& song = engine.getSong();
    patternBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < song.patterns.size(); ++i)
        patternBox.addItem (juce::String::fromUTF8 (song.patterns[i].name.c_str()), (int) i + 1);

    patternBox.setSelectedItemIndex (song.currentPattern, juce::dontSendNotification);
    swingSlider.setValue (song.swing, juce::dontSendNotification);
    tempoDisplay.repaint();
    refreshModeButton();

    refreshRackPatternSelector();
    refreshRackLengthBox();
}

void MainComponent::refreshRackPatternSelector()
{
    const auto& song = engine.getSong();
    const auto valid = song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size();
    const auto text = valid ? juce::String::fromUTF8 (song.patterns[(std::size_t) song.currentPattern].name.c_str()) : juce::String();
    const auto colour = valid ? PlaylistComponent::patternColour (song, song.currentPattern) : juce::Colour();

    if (text != rackPatternSelector.text || colour != rackPatternSelector.colour)
    {
        rackPatternSelector.text = text;
        rackPatternSelector.colour = colour;
        rackPatternSelector.repaint();
    }
}

void MainComponent::showRackPatternList()
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;

    for (int i = 0; i < (int) song.patterns.size(); ++i)
        menu.addItem (juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str()), true, i == song.currentPattern, [this, i] { selectPattern (i); });

    menu.addSeparator();
    menu.addItem (tr ("New pattern"), [this] { newPatternButton.onClick(); });
    menu.addItem (tr ("Rename and color the pattern") + shortcutText ("pattern.rename"), [this] { runAction ("pattern.rename"); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rackPatternSelector));
}

// --- the Channel Rack's pattern length box (issue #12) ---------------------------------------------------------

int MainComponent::PatternLengthBox::stepBars (int value, int delta)
{
    static const int steps[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
    constexpr int count = (int) (sizeof (steps) / sizeof (steps[0]));
    int i = 0;
    while (i + 1 < count && steps[i + 1] <= value)
        ++i;
    if (delta < 0 && steps[i] < value) // between two steps: going down lands on the lower one
        ++delta;
    return steps[juce::jlimit (0, count - 1, i + delta)];
}

void MainComponent::PatternLengthBox::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds().toFloat();
    g.setColour (hovered ? colour::bg3 : colour::bg1);
    g.fillRoundedRectangle (area, radius::sm);
    g.setColour (hovered ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (area.reduced (0.5f), radius::sm, 1.0f);
    auto r = getLocalBounds().reduced (3, 0);
    // round 170e: a length icon instead of the letters "A" / "L" (unreadable); dim = automatic (from the notes)
    icons::draw (g, Icon::span, r.removeFromLeft (13).toFloat().withSizeKeepingCentre (13.0f, 13.0f),
                 automatic ? colour::textDisabled : colour::textSecondary);
    g.setFont (font (type::small, true));
    g.setColour (automatic ? colour::textSecondary : colour::textPrimary);
    g.drawText (juce::String (bars), r, juce::Justification::centred, false);
}

void MainComponent::PatternLengthBox::mouseDown (const juce::MouseEvent& e)
{
    dragStartBars = bars;
    if (e.mods.isPopupMenu() && onMenu)
        onMenu();
}

void MainComponent::PatternLengthBox::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || onSet == nullptr)
        return;
    // one step of the list per 14 px up (as the rack's insert number box: drag up = more)
    const auto delta = -e.getDistanceFromDragStartY() / 14;
    const auto value = delta == 0 ? dragStartBars : stepBars (dragStartBars, delta);
    if (value != bars || automatic)
        if (delta != 0)
            onSet (value);
}

void MainComponent::PatternLengthBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (onSet != nullptr && wheel.deltaY != 0.0f)
        onSet (stepBars (bars, wheel.deltaY > 0.0f ? 1 : -1));
}

void MainComponent::refreshRackLengthBox()
{
    const auto& song = engine.getSong();
    if (song.currentPattern < 0 || song.currentPattern >= (int) song.patterns.size())
        return;
    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
    const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
    const auto bars = (int) juce::jmax<std::int64_t> (1, (pattern.effectiveLength (song.beatsPerBar) + bar - 1) / bar);
    const auto automatic = pattern.lengthTicks == 0;
    if (bars != rackLengthBox.bars || automatic != rackLengthBox.automatic)
    {
        rackLengthBox.bars = bars;
        rackLengthBox.automatic = automatic;
        rackLengthBox.setTooltip (automatic ? tr ("Pattern length: {0} (automatic, from the notes)", { bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }) })
                                            : tr ("Pattern length: {0}", { bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }) }));
        rackLengthBox.repaint();
    }
}

void MainComponent::showPatternLengthMenu()
{
    const auto& song = engine.getSong();
    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
    const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Pattern length"));
    menu.addItem (tr ("Automatic (from notes)"), true, pattern.lengthTicks == 0, [this] { rackLengthBox.onSet (0); });
    for (int bars : { 1, 2, 4, 8, 16, 32 })
        menu.addItem (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }), true, pattern.lengthTicks == bars * bar,
                      [this, bars] { rackLengthBox.onSet (bars); });
    menu.addSeparator();
    menu.addItem (tr ("Type value..."), [this] {
        ui::ValueSpec spec;
        spec.name = tr ("Pattern length (bars, 0 = automatic)");
        spec.minimum = 0.0;
        spec.maximum = 999.0;
        spec.interval = 1.0;
        ui::typeValue (rackLengthBox, spec, rackLengthBox.automatic ? 0.0 : (double) rackLengthBox.bars,
                       [this] (double v) { rackLengthBox.onSet (juce::roundToInt (v)); });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rackLengthBox));
}

void MainComponent::PatternSelector::paint (juce::Graphics& g)
{
    using namespace theme;
    auto area = getLocalBounds().toFloat();
    g.setColour (hoveredPart >= 0 ? colour::bg3 : colour::bg1);
    g.fillRoundedRectangle (area, radius::sm);
    g.setColour (hoveredPart >= 0 ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (area.reduced (0.5f), radius::sm, 1.0f);

    // the pattern's colour as a thin stripe at the left of the name
    if (! colour.isTransparent())
    {
        g.setColour (colour);
        g.fillRect (juce::Rectangle<float> ((float) arrowWidth + 1.0f, 4.0f, 3.0f, (float) getHeight() - 8.0f));
    }

    auto arrow = [&g, this] (int part, bool left) {
        const auto box = juce::Rectangle<float> (left ? 0.0f : (float) (getWidth() - arrowWidth), 0.0f, (float) arrowWidth, (float) getHeight());
        const auto c = box.getCentre();
        juce::Path p;
        if (left)
            p.addTriangle (c.x + 2.5f, c.y - 4.0f, c.x + 2.5f, c.y + 4.0f, c.x - 2.5f, c.y);
        else
            p.addTriangle (c.x - 2.5f, c.y - 4.0f, c.x - 2.5f, c.y + 4.0f, c.x + 2.5f, c.y);
        g.setColour (hoveredPart == part ? colour::textPrimary : colour::textSecondary);
        g.fillPath (p);
    };
    arrow (0, true);
    arrow (2, false);

    g.setColour (colour::textPrimary);
    g.setFont (font (type::small, true));
    const auto name = getLocalBounds().withTrimmedLeft (arrowWidth + 7).withTrimmedRight (arrowWidth + 14);
    g.drawText (text, name, juce::Justification::centredLeft, true);
    icons::draw (g, Icon::chevronDown, juce::Rectangle<float> ((float) (getWidth() - arrowWidth - 14), 0.0f, 12.0f, (float) getHeight()).reduced (1.0f, 4.0f),
                 hoveredPart == 1 ? colour::textPrimary : colour::textSecondary);
    g.setColour (colour::border);
    g.fillRect (arrowWidth, 3, 1, getHeight() - 6);
    g.fillRect (getWidth() - arrowWidth - 1, 3, 1, getHeight() - 6);
}

void MainComponent::PatternSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto part = partAt (e.x);
    namePressed = dragging = false;
    appliedSteps = 0;

    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
    }
    else if (part == 1)
        namePressed = true; // the list opens on release unless the mouse dragged (FL's pattern selector)
    else if (onStep)
        onStep (part == 0 ? -1 : 1);
}

void MainComponent::PatternSelector::mouseDrag (const juce::MouseEvent& e)
{
    if (! namePressed)
        return;
    dragging = dragging || std::abs (e.getDistanceFromDragStartY()) >= patterndrag::threshold;
    if (! dragging)
        return;
    const auto steps = patterndrag::steps ((float) e.getDistanceFromDragStartY());
    if (steps != appliedSteps && onStep)
        onStep (steps - appliedSteps);
    appliedSteps = steps;
}

void MainComponent::PatternSelector::mouseUp (const juce::MouseEvent&)
{
    if (namePressed && ! dragging && onMenu)
        onMenu();
    namePressed = dragging = false;
}

void MainComponent::PatternSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;
    if (const auto n = wheelSteps.take (delta); n != 0 && onStep)
        onStep (n);
}

void MainComponent::PatternBox::mouseDown (const juce::MouseEvent& e)
{
    pressed = dragging = false;
    appliedSteps = 0;
    if (! isEnabled() || e.mods.isPopupMenu())
        return;
    pressed = true; // the list opens on release unless the mouse dragged (FL's pattern selector)
}

void MainComponent::PatternBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressed)
        return;
    dragging = dragging || std::abs (e.getDistanceFromDragStartY()) >= patterndrag::threshold;
    if (! dragging)
        return;
    const auto steps = patterndrag::steps ((float) e.getDistanceFromDragStartY());
    if (steps != appliedSteps && onStep)
        onStep (steps - appliedSteps);
    appliedSteps = steps;
}

void MainComponent::PatternBox::mouseUp (const juce::MouseEvent&)
{
    if (pressed && ! dragging)
        showPopup();
    pressed = dragging = false;
}

void MainComponent::PatternBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;
    if (wheel.isInertial)
        return;
    if (const auto n = wheelSteps.take (delta); n != 0 && onStep)
        onStep (n);
}

juce::PopupMenu MainComponent::playlistTrackExportMenu (bool groups)
{
    // FL: From song start... / From track start... / Time selection... (the last does nothing without a time selection)
    juce::PopupMenu menu;
    auto add = [&] (const juce::String& text, quickrender::Start start, bool enabled) {
        menu.addItem (text, enabled, false, [this, start, groups] { exportAllPlaylistTracks (engine, projectName, start, groups); });
    };
    add (tr ("From song start..."), quickrender::Start::songStart, true);
    add (tr ("From track start..."), quickrender::Start::clipStart, true);
    add (tr ("Time selection..."), quickrender::Start::timeSelection, engine.getSong().hasLoop());
    return menu;
}

juce::PopupMenu MainComponent::makePatternMenu (std::vector<int> picked)
{
    const auto& song = engine.getSong();
    const auto index = song.currentPattern;
    const auto& pattern = song.patterns[(std::size_t) index];

    auto colours = ui::colourMenu (pattern.colour, [this, index] (std::uint32_t c) {
        if (index < (int) engine.getSong().patterns.size())
            engine.editSong ([index, c] (Song& s) { s.patterns[(std::size_t) index].colour = c; });
    }, juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1));

    juce::PopupMenu lengths;
    lengths.addItem (tr ("Automatic (from notes)"), true, pattern.lengthTicks == 0,
                     [this, index] { engine.editSong ([index] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = 0; }); });

    for (int bars : { 1, 2, 4, 8, 16, 32 })
    {
        const auto ticks = (std::int64_t) bars * ppq * juce::jmax (1, song.beatsPerBar);
        lengths.addItem (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }), true, pattern.lengthTicks == ticks,
                         [this, index, ticks] { engine.editSong ([index, ticks] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = ticks; }); });
    }

    juce::PopupMenu menu;
    // a double-click on the title renames the pattern (issue #12)
    ui::addItemHeader (menu, tr ("Pattern"), juce::String::fromUTF8 (pattern.name.c_str()), PlaylistComponent::patternColour (song, index),
                       [this, index] { showRenamePattern (index); });
    menu.addItem (tr ("Next pattern") + shortcutText ("pattern.next"), index + 1 < (int) song.patterns.size(), false,
                  [this] { selectPattern (stepPattern (engine.getSong(), 1)); });
    menu.addItem (tr ("Previous pattern") + shortcutText ("pattern.previous"), index > 0, false,
                  [this] { selectPattern (stepPattern (engine.getSong(), -1)); });
    menu.addItem (tr ("First empty pattern (and name it)") + shortcutText ("pattern.firstEmpty"), [this] { runAction ("pattern.firstEmpty"); });
    menu.addItem (tr ("Next empty pattern (and name it)") + shortcutText ("pattern.nextEmpty"), [this] { nextEmptyPattern(); });
    menu.addItem (tr ("Next empty pattern (no naming)") + shortcutText ("pattern.nextEmptyNoName"), [this] { runAction ("pattern.nextEmptyNoName"); });
    menu.addSeparator();
    menu.addItem (tr ("Rename and color...") + shortcutText ("pattern.rename"), [this, index] { showRenamePattern (index); });
    menu.addSubMenu (tr ("Color"), colours);
    menu.addSubMenu (tr ("Length"), lengths);
    menu.addItem (tr ("Insert an empty pattern before this one") + shortcutText ("pattern.insert"), [this] { insertPatternHere(); });
    menu.addItem (tr ("Clone") + shortcutText ("pattern.clone"), [this] { clonePatternNow(); });
    menu.addItem (tr ("Move up") + shortcutText ("pattern.moveUp"), index > 0, false, [this] { movePatternNow (-1); });
    menu.addItem (tr ("Move down") + shortcutText ("pattern.moveDown"), index + 1 < (int) song.patterns.size(), false, [this] { movePatternNow (1); });
    menu.addItem (tr ("Split by channel (one new pattern per channel)"), ! pattern.notes.empty(), false, [this] { splitPatternByChannelNow(); });
    // FL's Patterns › PSPatQuickRenderMenu / PSPatRenderMenu / PSPatRenderReplaceMenu (PSPatQuickRenderMenuClick, tags 1 / 0 / 2;
    // REA round 168e); opened from the picker, every picked pattern is rendered (round 169k)
    if (picked.empty())
        picked = { index };
    menu.addItem (tr ("Quick render as audio clip"), [this, picked] { renderPatternsAsAudioClips (engine, picked, 1); });
    menu.addItem (tr ("Render as audio clip..."), [this, picked] { renderPatternsAsAudioClips (engine, picked, 0); });
    menu.addItem (tr ("Render and replace..."), [this, picked] { renderPatternsAsAudioClips (engine, picked, 2); });
    juce::PopupMenu signatures;

    for (int beats : { 2, 3, 4, 5, 6, 7, 8, 9, 12 })
        signatures.addItem (juce::String (beats) + "/4", true, song.beatsPerBar == beats,
                            [this, beats] { engine.editSong ([beats] (Song& s) { s.beatsPerBar = beats; }); });

    menu.addSubMenu (tr ("Song time signature"), signatures);
    menu.addSeparator();
    menu.addItem (tr ("Delete pattern") + shortcutText ("pattern.delete"), song.patterns.size() > 1, false,
                  [this, index] { engine.editSong ([index] (Song& s) { s.removePattern (index); }); });
    return menu;
}

void MainComponent::showPatternMenu()
{
    makePatternMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternMenuButton));
}

void MainComponent::refreshModeButton()
{
    const auto recording = engine.isRecording();
    recordButton.setToggleState (recording, juce::dontSendNotification);
    const auto inputs = engine.getActiveMidiInputs();
    recordButton.setTooltip ((engine.isAudioRecordingArmed() ? tr ("Record while playing: notes (PC keyboard and MIDI) and input audio")
                                                             : tr ("Record while playing: notes (PC keyboard and MIDI)"))
                             + "\n" + tr ("MIDI inputs: {0}", { inputs.isEmpty() ? tr ("none") : inputs.joinIntoString (", ") }));
    recordMenuButton.setToggleState (engine.isAudioRecordingArmed(), juce::dontSendNotification);
    recordMenuButton.setOnColour (theme::colour::accentMuted);

    const auto songMode = engine.getSong().mode == PlayMode::song;
    patternModeButton.setToggleState (! songMode, juce::dontSendNotification);
    songModeButton.setToggleState (songMode, juce::dontSendNotification);
    metronomeButton.setToggleState (engine.getTransport().metronome.load(), juce::dontSendNotification);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (const auto notice = engine.takeRecordingNotice(); notice.issue != AudioEngine::InputIssue::none && persistent)
        mixerinput::explain (notice);
    // round 165c: hardware inserts measured for another device setup (the device or its buffer size changed, or the project
    // was measured elsewhere): a prompt that pings them on request, nothing pinged by itself (pings make clicks)
    if (auto stale = engine.takeHardwareInsertNotice(); ! stale.empty() && persistent)
    {
        const auto count = (int) stale.size();
        auto options = juce::MessageBoxOptions()
                           .withIconType (juce::MessageBoxIconType::InfoIcon)
                           .withTitle (tr ("Hardware Insert"))
                           .withMessage ((count == 1 ? tr ("The round trip of a hardware insert was measured for another audio setup (the device or its "
                                                           "buffer size changed), so its return is no longer lined up with the rest of the mix.")
                                                     : tr ("The round trip of {0} hardware inserts was measured for another audio setup (the device or its "
                                                           "buffer size changed), so their returns are no longer lined up with the rest of the mix.",
                                                           { juce::String (count) }))
                                         + "\n\n" + tr ("Ping now? Each one sends a few clicks out to its gear and listens for them on its return."))
                           .withButton (tr ("Ping now"))
                           .withButton (tr ("Later"));
        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), stale = std::move (stale)] (int result) {
            if (result == 1 && safe != nullptr)
                safe->engine.pingHardwareInserts (stale);
        });
    }

    const auto& song = engine.getSong();
    // FL's "Select first channel with notes when the pattern changes" (Settings › General, REA round 167j §5)
    if (song.currentPattern != lastCurrentPattern)
    {
        const auto changedPattern = lastCurrentPattern >= 0;
        lastCurrentPattern = song.currentPattern;
        if (changedPattern && ChannelRackComponent::selectFirstChannelWithNotes)
            if (const auto first = firstChannelWithNotes (song, song.currentPattern); first >= 0 && first != engine.getSelectedChannel())
                engine.setSelectedChannel (first);
    }
    bool namesChanged = patternBox.getNumItems() != (int) song.patterns.size();

    for (int i = 0; ! namesChanged && i < patternBox.getNumItems(); ++i)
        namesChanged = patternBox.getItemText (i) != juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str());

    refreshModeButton();
    tempoDisplay.repaint();

    // a project with other filter groups (or none): the rack's filter falls back to "All"
    if (rack.getFilterGroup() >= (int) song.channelGroups.size())
        rack.setFilterGroup (-1);
    if (rackFilterBox.text != rack.filterName())
    {
        rackFilterBox.text = rack.filterName();
        rackFilterBox.repaint();
    }
    if (rackLoopButton.getToggleState() != rack.isLoopingOn()) // (another pattern, undo)
        rackLoopButton.setToggleState (rack.isLoopingOn(), juce::dontSendNotification);

    if ((int) song.channels.size() != fittedChannelCount && fittedChannelCount >= 0) // a row was added or removed
        fitChannelRack (false);

    if (namesChanged || patternBox.getSelectedItemIndex() != song.currentPattern || std::abs (swingSlider.getValue() - song.swing) > 1e-6)
        refreshToolbar();
    else
    {
        refreshRackPatternSelector(); // a new pattern colour
        refreshRackLengthBox();       // (notes added: the automatic length grows)
    }
}

// --- tempo display -------------------------------------------------------------------------------

ui::ValueSpec MainComponent::TempoDisplay::spec() const
{
    ui::ValueSpec s;
    s.name = tr ("Tempo");
    s.minimum = minTempo;
    s.maximum = maxTempo;
    s.defaultValue = 130.0;
    s.toText = [] (double v) { return juce::String (v, 3) + " BPM"; };
    s.target = AutomationTarget { AutomationTargetKind::tempo, 0 };
    return s;
}

void MainComponent::TempoDisplay::setBpm (double bpm)
{
    bpm = controls::clampTempo (bpm);

    if (std::abs (bpm - engine.getSong().bpm) > 1e-9)
        engine.editSong ([bpm] (Song& s) { clipops::followTempo (s, bpm); });

    repaint();
}

bool MainComponent::TempoDisplay::isOnDecimals (int x) const
{
    return decimalX > 0 && x > decimalX;
}

void MainComponent::TempoDisplay::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds().toFloat();
    g.setColour (colour::bg1);
    g.fillRoundedRectangle (area, radius::md);

    if (dragging)
    {
        g.setColour (colour::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius::md, 1.0f);
    }

    const auto bpm = engine.getSong().bpm;
    const auto whole = juce::String ((int) std::floor (bpm + 1e-9));
    const auto fraction = juce::String (bpm - std::floor (bpm + 1e-9), 3).fromFirstOccurrenceOf (".", false, false).paddedRight ('0', 3).substring (0, 3);

    const juce::Font big (monoFont (18.0f)), small (monoFont (11.0f)), caption (font (type::caption - 1.0f));
    const auto wholeWidth = juce::GlyphArrangement::getStringWidth (big, whole);
    const auto fracWidth = juce::GlyphArrangement::getStringWidth (small, "." + fraction);
    const auto unitWidth = juce::GlyphArrangement::getStringWidth (caption, "BPM");
    auto x = area.getCentreX() - (wholeWidth + fracWidth + 6.0f + unitWidth) / 2.0f;
    const auto baseline = area.getCentreY() + 5.0f;

    g.setColour (colour::textPrimary);
    g.setFont (big);
    g.drawSingleLineText (whole, juce::roundToInt (x), juce::roundToInt (baseline));
    x += wholeWidth;
    decimalX = juce::roundToInt (x);
    g.setColour (dragOnDecimals && dragging ? colour::textPrimary : colour::textSecondary);
    g.setFont (small);
    g.drawSingleLineText ("." + fraction, juce::roundToInt (x), juce::roundToInt (baseline));
    x += fracWidth + 6.0f;
    g.setColour (colour::textDisabled);
    g.setFont (caption);
    g.drawSingleLineText ("BPM", juce::roundToInt (x), juce::roundToInt (baseline));
}

void MainComponent::TempoDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (ui::isResetClick (e))
    {
        setBpm (spec().defaultValue);
        return;
    }

    startBpm = engine.getSong().bpm;
    dragOnDecimals = isOnDecimals (e.x);
    dragging = true;
    repaint();
}

void MainComponent::TempoDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
    {
        engine.noteTweaked ({ AutomationTargetKind::tempo, 0 });
        setBpm (controls::tempoDrag (startBpm, -e.getDistanceFromDragStartY(), dragOnDecimals, e.mods.isCtrlDown()));
    }
}

void MainComponent::TempoDisplay::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void MainComponent::TempoDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        ui::typeValue (*this, spec(), engine.getSong().bpm, [this] (double v) { setBpm (v); });
}

void MainComponent::TempoDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;

    if (delta == 0.0f)
        return;

    const auto step = isOnDecimals (e.x) ? 0.01 : 1.0;
    engine.noteTweaked ({ AutomationTargetKind::tempo, 0 });
    setBpm (engine.getSong().bpm + (delta > 0.0f ? 1.0 : -1.0) * step * (e.mods.isCtrlDown() ? 0.1 : 1.0));
}

ui::Hint MainComponent::TempoDisplay::hintAt (juce::Point<int> p)
{
    return ui::valueHint (spec(), engine.getSong().bpm,
                          isOnDecimals (p.x) ? tr ("[LMB] drag the decimals for fine changes  [Wheel] 0.01  [2xLMB] type  [RMB] tap tempo, half, double")
                                             : tr ("[LMB] drag the whole number  [Ctrl] fine  [Wheel] 1 BPM  [2xLMB] type  [RMB] tap tempo, half, double"));
}

void MainComponent::TempoDisplay::showMenu()
{
    const auto bpm = engine.getSong().bpm;
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Tempo") + ": " + juce::String (bpm, 3) + " BPM");
    menu.addItem (tr ("Type value..."), [this, bpm] { ui::typeValue (*this, spec(), bpm, [this] (double v) { setBpm (v); }); });
    menu.addItem (tr ("Tempo tapper..."), [this] { showTap(); });
    menu.addItem (tr ("Half ({0} BPM)", { juce::String (controls::halfTempo (bpm), 2) }), [this, bpm] { setBpm (controls::halfTempo (bpm)); });
    menu.addItem (tr ("Double ({0} BPM)", { juce::String (controls::doubleTempo (bpm), 2) }), [this, bpm] { setBpm (controls::doubleTempo (bpm)); });
    menu.addItem (tr ("Reset ({0})", { "130 BPM" }), [this] { setBpm (130.0); });
    menu.addSeparator();
    addAutomationItem (menu, engine, { AutomationTargetKind::tempo, 0 });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void MainComponent::TempoDisplay::showTap()
{
    openTempoTapper (engine); // FL's tempo menu opens its tempo tapper too
}

// --- hint bar ------------------------------------------------------------------------------------

juce::String MainComponent::idleHint() const
{
    if (engine.isWaitingForInput())
        return tr ("Recording: waiting for the first note ([Space] cancels)");

    if (engine.isCountingIn())
        return tr ("Recording: count-in...");

    if (const auto scan = engine.getPluginHost().backgroundScan(); scan.running)
        return tr ("Scanning for plugins in the background: {0}% {1}", { juce::String (juce::roundToInt (scan.progress * 100.0f)), scan.file });

    // round 170e3: one line at 1920 (the typing piano's state shows on its own button)
    return tr ("Point at anything for help  [F1] shortcuts  [F5]-[F9] windows  [Space] play/stop");
}

juce::String MainComponent::keyMarkup (const char* actionId) const
{
    const auto& chords = keyMap.chordsFor (actionId);

    if (chords.empty())
        return {};

    // "Ctrl+Shift+S" -> "[Ctrl][Shift][S]"; a key name that is not markup stays as text
    const auto text = juce::String (keys::displayString (chords.front()));
    juce::String out;

    for (const auto& part : juce::StringArray::fromTokens (text, "+", ""))
    {
        if (! hintmarkup::isToken (part.toStdString()))
            return "(" + text + ")";
        out << "[" << part << "]";
    }

    return out;
}

void MainComponent::setControlHints()
{
    auto with = [this] (const juce::String& text, const char* actionId) {
        const auto key = keyMarkup (actionId);
        return key.isEmpty() ? text : text + "  " + key;
    };

    ui::setHint (newButton, with (tr ("New project"), "file.new"));
    ui::setHint (openButton, with (tr ("Open a project (.lynx, FL Studio .flp, MIDI)"), "file.open"));
    ui::setHint (saveButton, with (tr ("Save the project"), "file.save") + "  " + tr ("save as:") + " " + keyMarkup ("file.saveAs"));
    ui::setHint (exportButton, with (tr ("Export audio: WAV, FLAC or OGG"), "file.export"));
    ui::setHint (undoButton, with (tr ("Undo the last edit"), "edit.undo") + "  " + tr ("[RMB] undo history"), true);
    ui::setHint (redoButton, with (tr ("Redo"), "edit.redo") + "  " + tr ("[RMB] undo history"), true);
    ui::setHint (pluginPickerButton, with (tr ("Plugin picker: add an instrument or an effect by name"), "panel.quickAdd"));

    ui::setHint (playButton, with (tr ("Play: [LMB] play; again: stop and go back to where it started"), "transport.playStop")
                                 + "  " + keyMarkup ("transport.pause") + " " + tr ("pause"));
    ui::setHint (stopButton, tr ("Stop: [LMB] stop and go back to the start (or the remembered position; again: the start)  "
                                 "[2xLMB] back to the start and all notes off (panic)  [RMB] remember seek time")
                                 + "  " + keyMarkup ("tools.stopSound") + " " + tr ("Stop sound (all notes off)"), // round 170h2
                 true);
    ui::setHint (recordButton, with (tr ("Record: [LMB] arm, then play to record notes (and audio, when armed)  [RMB] what to record"), "transport.record"),
                 true);
    ui::setHint (recordMenuButton, tr ("What to record: notes, input audio, count-in, overdub or replace, quantise  [LMB] menu"), true);
    ui::setHint (patternModeButton, with (tr ("Pattern mode: plays the current pattern in a loop  [RMB] show the Channel Rack"), "transport.toggleMode"));
    ui::setHint (songModeButton, with (tr ("Song mode: plays the Playlist arrangement  [RMB] show the Playlist"), "transport.toggleMode"));
    ui::setHint (metronomeButton, with (tr ("Metronome: clicks on every beat while playing  [RMB] volume and options"), "transport.metronome"), true);
    ui::setHint (countInButton, with (tr ("Count-in: one bar of clicks before recording starts (the menu has more lengths)"), "record.countIn"));
    ui::setHint (waitInputButton, with (tr ("Wait for input: recording starts with the first note you play"), "record.waitForInput"));
    ui::setHint (blendButton, tr ("Blend recorded notes: on, a take adds its notes to the pattern (overdub); off, a recorded channel's old notes are replaced"));
    ui::setHint (loopRecordButton, with (tr ("Loop record: on, recording loops (Song mode: the loop region, one take per pass; Pattern mode: the pattern); off, it "
                                             "plays straight on, through the loop region or past the pattern's end, and keeps everything where it "
                                             "was played (the pattern grows)"), "record.loop"));
    ui::setHint (stepEditButton, with (tr ("Step edit: with the transport stopped, notes you play go into the current pattern at the position, "
                                           "a snap step long; release the keys and the position moves on one step (chords: hold them together)"),
                                       "record.stepEdit"));
    ui::setHint (punchButton, with (tr ("Punch in/out: audio recording keeps only the loop region (playback runs through it; pre- and "
                                        "post-roll in the recording options)"), "record.punch"));
    ui::setHint (tapTempoButton, with (tr ("Tempo tapper: tap the beat to set the tempo"), "view.tempoTapper"));
    ui::setHint (typingPianoButton, with (tr ("Typing keyboard to piano: Z..M and Q..P play the selected channels; letter shortcuts pause"),
                                          "view.typingPiano"));
    ui::setHint (snapToggleButton, tr ("Snap (magnet): [LMB] turns the snap off and back on (as [Backspace]); lit while it snaps. "
                                       "The Playlist and Piano Roll snap boxes show the same magnet"));
    ui::setHint (gridBox, tr ("Global snap: the grid of the Piano Roll and Playlist (when they follow it) and the quantise of recorded notes  "
                              "[LMB] choose  [Wheel] step  [RMB] length of new notes  [Backspace] snap on/off  [Alt] while dragging: no snap"),
                 true);
    ui::setHint (patternBox, tr ("Current pattern: what the Channel Rack and Piano Roll edit and Pattern mode plays  [LMB] choose  [Wheel] step  "
                                 "[F4] next empty  [F2] rename") + "  " + tr ("[LMB] drag up / down: step through the patterns"));
    ui::setHint (newPatternButton, tr ("New pattern: an empty pattern after the last one"));
    ui::setHint (patternMenuButton, tr ("Pattern menu: rename, color, length, clone, insert, split by channel, delete"), true);

    const char* panelHelp[] = { TR_NOOP ("the arrangement: clips of patterns, audio and automation on tracks"),
                                TR_NOOP ("the instruments and their step sequencer"),
                                TR_NOOP ("the notes of the selected channel in the current pattern"),
                                TR_NOOP ("inserts, effects and routing"),
                                TR_NOOP ("samples, presets, plugins and projects to drag in") };
    for (int i = 0; i < workspace::panelCount; ++i)
        ui::setHint (panelButtons[(std::size_t) i],
                     with (panelName ((Panel) i) + ": " + tr (panelHelp[i]) + "  " + tr ("[LMB] show, focus or hide  [RMB] maximized"),
                           panelAction ((Panel) i)));

    setQuickTips();
}

std::vector<std::pair<juce::Rectangle<int>, juce::String>> MainComponent::getQuickTipsForSnapshot()
{
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> tips;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible())
                continue;
            if (const auto tip = ui::quickTipOf (*child); tip.isNotEmpty() && ! child->getBounds().isEmpty())
                tips.emplace_back (getLocalArea (child->getParentComponent(), child->getBounds()), tip);
            visit (*child);
        }
    };
    visit (*this);
    return tips;
}

// the small name bubble of every toolbar control (the hint bar keeps the full description)
void MainComponent::setQuickTips()
{
    auto tip = [this] (juce::Component& c, const juce::String& name, const char* actionId = nullptr) {
        ui::setQuickTip (c, name, actionId != nullptr ? shortcutText (actionId).trim() : juce::String());
    };

    tip (newButton, tr ("New project"), "file.new");
    tip (openButton, tr ("Open project"), "file.open");
    tip (saveButton, tr ("Save project"), "file.save");
    tip (exportButton, tr ("Export audio"), "file.export");
    tip (undoButton, tr ("Undo"), "edit.undo");
    tip (redoButton, tr ("Redo"), "edit.redo");
    tip (pluginPickerButton, tr ("Plugin picker"), "panel.quickAdd");
    tip (playButton, tr ("Play"), "transport.playStop");
    tip (stopButton, tr ("Stop"));
    tip (recordButton, tr ("Record"), "transport.record");
    tip (recordMenuButton, tr ("Recording options"));
    tip (patternModeButton, tr ("Pattern mode"), "transport.toggleMode");
    tip (songModeButton, tr ("Song mode"), "transport.toggleMode");
    tip (metronomeButton, tr ("Metronome"), "transport.metronome");
    tip (countInButton, tr ("Count-in"), "record.countIn");
    tip (waitInputButton, tr ("Wait for input"), "record.waitForInput");
    tip (blendButton, tr ("Blend recorded notes"), "record.blend");
    tip (loopRecordButton, tr ("Loop record"), "record.loop");
    tip (stepEditButton, tr ("Step edit"), "record.stepEdit");
    tip (punchButton, tr ("Punch in/out"), "record.punch");
    tip (tapTempoButton, tr ("Tempo tapper"), "view.tempoTapper");
    tip (typingPianoButton, tr ("Typing keyboard to piano"), "view.typingPiano");
    tip (tempoDisplay, tr ("Tempo (BPM)"));
    tip (timeDisplay, tr ("Song time"));
    tip (songPosition, tr ("Song position"));
    tip (masterVolume, tr ("Master volume"));
    tip (masterPitch, tr ("Master pitch"));
    tip (gridBox, tr ("Snap"));
    tip (snapToggleButton, tr ("Snap on / off"));
    tip (patternBox, tr ("Pattern"));
    tip (newPatternButton, tr ("New pattern"));
    tip (patternMenuButton, tr ("Pattern options"));
    tip (perfMeter, tr ("CPU, memory and polyphony"));
    tip (chordPanel, tr ("Detected chord"));
    tip (output, tr ("Output monitor"));
    tip (swingSlider, tr ("Swing"));
    tip (rackFilterBox, tr ("Channel filter group"));
    tip (rackGraphButton, tr ("Graph editor"));
    tip (rackPianoRollButton, tr ("Piano-roll view"), "rack.pianoRollView");
    tip (rackLoopButton, tr ("Channel looping"));
    tip (rackLoopStarterButton, tr ("Loop Starter"));

    for (int i = 0; i < workspace::panelCount; ++i)
        tip (panelButtons[(std::size_t) i], tr (panelName ((Panel) i)), panelAction ((Panel) i));
}

// FL's Step edit (round 170e3): the notes played since the last tick go into the current pattern at the position, a
// snap step of the Piano Roll long; when every key is up the position moves on a step (core/StepEdit.h). While the
// transport plays they are left to the recording, as in FL.
void MainComponent::applyStepEdit()
{
    auto played = engine.takeStepEditNotes();
    if (played.empty() || engine.getTransport().playing.load())
        return;

    const auto now = engine.getTransport().positionTicks.load();
    // the position moved by anything else (a click on a ruler, Stop): start from there
    if (now != stepSeekTick && now != stepPrevTick)
    {
        const auto& song = engine.getSong();
        const auto at = transportedit::patternPlayhead (song, song.currentPattern, now);
        stepWriter.reset (at >= 0.0 ? (std::int64_t) std::llround (at) : 0);
        stepSeekTick = stepPrevTick = now;
    }

    // the toolbar (main) snap is FL's step-edit step (round 170e5)
    const auto& mainSnap = grid::options()[(std::size_t) juce::jlimit (0, (int) grid::options().size() - 1, EditorGlobals::get().getGridOption())];
    const auto mainTicks = mainSnap.kind == grid::Kind::ticks ? (std::int64_t) mainSnap.ticks
                           : mainSnap.kind == grid::Kind::bar ? (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar)
                                                              : 0;
    const auto step = stepedit::stepLength (mainTicks, pianoRoll.gridForSnapshot());
    for (const auto& p : played)
    {
        auto moved = false;
        engine.editSong ([&] (Song& s) {
            if (! s.patterns.empty())
                moved = stepWriter.play (p, s.patterns[(std::size_t) juce::jlimit (0, (int) s.patterns.size() - 1, s.currentPattern)], step);
        }, p.on);
        if (! moved)
            continue;
        const auto& song = engine.getSong();
        const auto target = transportedit::patternSeekTarget (song, song.currentPattern, (double) stepWriter.getPosition(), now);
        if (target >= 0.0)
        {
            stepPrevTick = engine.getTransport().positionTicks.load();
            stepSeekTick = target;
            engine.seek (target);
        }
    }
}

bool MainComponent::toolbarFrame (double nowMs)
{
    const auto moved = output.update (nowMs);
    timeDisplay.update();
    songPosition.update();
    return moved || engine.getTransport().playing.load (std::memory_order_relaxed);
}

void MainComponent::refreshChordOptions()
{
    auto options = chordReadout.getOptions();
    options.sharps = ! notenames::flats();
    for (int pc = 0; pc < 12; ++pc)
    {
        // the chosen note names, with FL's accidentals ("C♯", "B♭", "Sol♯")
        auto name = juce::String (notenames::pitchName (pc));
        if (name.endsWithChar ('#'))
            name = name.dropLastCharacters (1) + juce::String::charToString ((juce::juce_wchar) 0x266F);
        else if (name.length() > 1 && name.endsWithChar ('b'))
            name = name.dropLastCharacters (1) + juce::String::charToString ((juce::juce_wchar) 0x266D);
        chordNotes[(std::size_t) pc] = name.toStdString();
    }
    options.notes = &chordNotes;
    // Roman (a LYNX choice, FL's item changes nothing): against the Piano Roll's scale, when one is set
    const auto& highlight = engine.getSong().scaleHighlight;
    options.tonic = pianoRoll.scaleTonic() >= 0 ? pianoRoll.scaleTonic() : highlight.scale >= 0 ? highlight.root : -1;
    chordReadout.setOptions (options);
}

void MainComponent::showChordText()
{
    auto text = juce::String::fromUTF8 (chordReadout.text().c_str());
    if (chordReadout.lastWasNovelty())
        text = tr (text);
    chordPanel.setText (text);

    if (chordShowNearHint)
    {
        // as FL: it follows the panel, leaves with "---", and with a display delay stays that long at most (15 s at most)
        const auto chord = chordReadout.text() != chordread::noChord && text.isNotEmpty();
        chordNearHint.setText (text);
        const auto width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body, true).withHeight (17.0f), text)) + 8;
        chordNearHint.setBounds (hintBar.getX() + 8, hintBar.getBottom() + 2, width, 24);
        chordNearHint.setVisible (chord);
        if (chord)
        {
            chordNearHint.toFront (false);
            chordNearHintUntil = juce::Time::getMillisecondCounter() + juce::jmin<std::uint32_t> (15000, chordReadout.getDelayMs());
        }
    }
}

void MainComponent::updateChordPanel()
{
    if (! chordPanel.isVisible() && ! chordShowNearHint)
        return;

    const auto now = juce::Time::getMillisecondCounter();
    auto changed = false;
    for (int channel = 0; channel < chordread::channels; ++channel)
    {
        const auto bits = engine.heldInputKeyBits (channel);
        auto& before = chordHeld[(std::size_t) channel];
        if (bits == before)
            continue;
        if (! changed)
            refreshChordOptions();
        changed = true;
        // what ended first: a chord changed between two ticks reads as the new chord, not as both
        for (const auto on : { false, true })
            for (int word = 0; word < 2; ++word)
            {
                auto diff = (on ? bits[(std::size_t) word] & ~before[(std::size_t) word] : before[(std::size_t) word] & ~bits[(std::size_t) word]);
                for (int b = 0; diff != 0; ++b, diff >>= 1)
                    if ((diff & 1u) != 0)
                        chordReadout.note (channel, word * 64 + b, on, now);
            }
        before = bits;
    }
    if (chordReadout.tick (now))
        changed = true;
    if (changed)
        showChordText();
    if (chordNearHint.isVisible() && chordReadout.getDelayMs() > 0 && (int) (now - chordNearHintUntil) > 0)
        chordNearHint.setVisible (false);
}

void MainComponent::showChordPanelMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Chord style"));
    const auto roman = chordReadout.getOptions().roman;
    auto setRoman = [this] (bool on) {
        auto o = chordReadout.getOptions();
        o.roman = on;
        chordReadout.setOptions (o);
        saveUiSettings();
    };
    menu.addItem (tr ("Standard"), true, ! roman, [setRoman] { setRoman (false); });
    menu.addItem (tr ("Roman"), true, roman, [setRoman] { setRoman (true); });
    menu.addSectionHeader (tr ("Display delay for played chords"));
    const auto delay = chordReadout.getDelayMs();
    auto delayItem = [&] (const juce::String& name, std::uint32_t seconds) {
        menu.addItem (name, true, delay == seconds * 1000u, [this, seconds] {
            chordReadout.setDelayMs (seconds * 1000u);
            saveUiSettings();
        });
    };
    delayItem (tr ("None"), 0);
    delayItem (tr ("1 second"), 1);
    delayItem (tr ("3 seconds"), 3);
    delayItem (tr ("5 seconds"), 5);
    delayItem (tr ("Show forever"), chordread::foreverSeconds);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chordPanel));
}

void MainComponent::addAudioLoggerMenu (juce::PopupMenu& menu)
{
    // FL 2026's Tools › Audio logger, item for item (REA_rodada181b.md)
    auto& log = engine.getRollingLog();
    const auto on = log.isEnabled();
    auto title = tr ("Audio logger");
    if (on)
        title << " (" << tr ("memory used: {0}MB", { juce::String (juce::roundToInt ((double) log.bytesInUse() / 1048576.0)) }) << ")";
    menu.addSectionHeader (title);
    menu.addItem (tr ("Enabled"), true, on, [this, on] {
        engine.getRollingLog().setEnabled (! on);
        saveUiSettings();
    });

    juce::PopupMenu settings;
    settings.addSectionHeader (tr ("Maximum length"));
    const auto seconds = log.lengthSeconds();
    auto lengthItem = [&] (const juce::String& name, int minutes) {
        settings.addItem (name, true, seconds == minutes * 60, [this, minutes] {
            engine.getRollingLog().setLengthSeconds (minutes * 60);
            saveUiSettings();
        });
    };
    lengthItem (tr ("1 minute"), 1);
    lengthItem (tr ("2 minutes"), 2);
    lengthItem (tr ("5 minutes"), 5);
    lengthItem (tr ("10 minutes"), 10);
    settings.addSectionHeader (tr ("Recording position"));
    const auto pre = log.getPosition() == RollingAudioLog::Position::preEffects;
    auto positionItem = [&] (const juce::String& name, RollingAudioLog::Position p, bool ticked) {
        settings.addItem (name, true, ticked, [this, p] {
            engine.getRollingLog().setPosition (p);
            saveUiSettings();
        });
    };
    positionItem (tr ("Pre effects"), RollingAudioLog::Position::preEffects, pre);
    positionItem (tr ("Post effects, pre fader"), RollingAudioLog::Position::postEffects, ! pre);
    menu.addSubMenu (tr ("Settings"), settings);
    menu.addItem (tr ("Dump to audio editor"), on, false, [this] { dumpAudioLog(); });
}

void MainComponent::dumpAudioLog()
{
    auto dump = std::make_shared<RollingAudioLog::Dump>();
    if (! engine.getRollingLog().dump (*dump))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Audio logger is empty"),
                                                tr ("The audio logger doesn't contain any audio at the moment"));
        return;
    }

    const auto folder = AudioLogger::defaultFolder();
    folder.createDirectory();
    const auto target = folder.getNonexistentChildFile ("Audiolog-" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);
    juce::Component::SafePointer<MainComponent> self (this);
    // (a long log takes a moment to write: not on the UI thread)
    juce::Thread::launch ([self, dump, target] {
        auto saved = false;
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (target.createOutputStream().release());
            if (stream != nullptr)
                if (std::unique_ptr<juce::AudioFormatWriter> writer { wav.createWriterFor (stream.get(), dump->rate, 2, 32, {}, 0) }; writer != nullptr)
                {
                    stream.release(); // owned by the writer now
                    const float* channels[] = { dump->left.data(), dump->right.data() };
                    saved = writer->writeFromFloatArrays (channels, 2, (int) dump->left.size());
                }
        }
        juce::MessageManager::callAsync ([self, target, saved] {
            if (self == nullptr)
                return;
            if (saved)
                openAudioEditor (self->engine, target);
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Audio logger"),
                                                        tr ("Could not create a file in {0}", { target.getParentDirectory().getFullPathName() }));
        });
    });
}

void MainComponent::timerCallback()
{
    const uitrace::Scope trace ("MainComponent timer", false);
    {
        const uitrace::Scope part ("timer: toolbar displays", false);
        masterVolume.update();
        masterPitch.update();
        updateRecordQuantise();
        snapToggleButton.setToggleState (EditorGlobals::get().getGridOption() != grid::noneOption, juce::dontSendNotification);
    }
    if (engine.isStepEdit())
        applyStepEdit();

    if (engine.isModified() != lastModified)
    {
        const uitrace::Scope part ("timer: title", false);
        lastModified = engine.isModified();
        updateTitle();
    }

    // the Browser's "Project folder" follows the project's file (.lynx, or the .flp it was imported from)
    const uitrace::Scope partFolders ("timer: project folder / recording target", false);
    browser.setProjectFile (currentFile != juce::File() ? currentFile : importedFlpFile);
    // and so does where audio is recorded ("<project folder>/Audio/Recorded", FL-like take names)
    engine.setRecordingTarget (recordingFolderFor (currentFile != juce::File() ? currentFile : importedFlpFile),
                               projectName.isNotEmpty() ? juce::File::createLegalFileName (projectName) : juce::String());

    // a backup copy every few minutes (Settings › General) when there are changes
    // (not in the middle of a recording: FL also postpones the timed backup until the transport is free; round 177d:
    // with "Wait with backups..." on, also while playing and for a few seconds after MIDI input, so writing the plugin
    // states never stutters the sound, up to the longest wait set there)
    if (const auto midi = engine.getMidiPlayed(); midi != backupMidiSeen)
    {
        backupMidiSeen = midi;
        backupMidiMs = juce::Time::getMillisecondCounterHiRes();
    }
    const auto backupDueTicks = 30 * 60 * autosaveMinutes;
    if (autosaveMinutes > 0 && ++autosaveTicks >= backupDueTicks
        && prefs::backupMayRun (engine.isRecording(), engine.getTransport().playing.load(),
                                juce::Time::getMillisecondCounterHiRes() - backupMidiMs, backupWaitsForQuiet,
                                autosaveTicks * 1000.0 / 30.0, backupLongestWaitMinutes * 60000.0))
    {
        autosaveTicks = 0;

        if (engine.isModified())
            autosave();
    }

    if (const auto scan = engine.getPluginHost().backgroundScan(); scan.finished != scansSeen)
    {
        scansSeen = scan.finished;
        ui::postHint ({ scan.found == 1 ? tr ("Plugin scan finished: 1 new plugin.")
                                        : tr ("Plugin scan finished: {0} new plugins.", { juce::String (scan.found) }), {}, false, false },
                      6000);
    }

    // a hosted plugin crashed inside a call: it was switched off (bypassed / silent) and LYNX kept running
    if (const auto crashes = pluginCrashCount(); crashes != pluginCrashesSeen)
    {
        pluginCrashesSeen = crashes;
        juce::StringArray names, lines;
        for (const auto& c : pluginCrashes())
        {
            const auto name = juce::String::fromUTF8 (c.plugin.c_str());
            names.addIfNotAlreadyThere (name);
            lines.add (juce::Time::getCurrentTime().toISO8601 (true) + "  " + name + "  " + crashguard::describe (c.code) + " 0x"
                       + juce::String::toHexString ((juce::int64) c.code));
        }
        PluginHost::settingsFolder().getChildFile ("plugin-crashes.log").appendText (lines[lines.size() - 1] + "\n");
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, tr ("Plugin crashed"),
            tr ("The plugin \"{0}\" crashed and was switched off, so LYNX SHARE and your project kept running (the effect is bypassed, an "
                "instrument stays silent). Save your project; the plugin loads again the next time you open it.",
                { names[names.size() - 1] }));
    }

    // round 176a: a plugin in a separate process (Make bridged, or a 32-bit plugin in the 32-bit bridge) crashed or hung:
    // only that process went; the plugin is muted. Round 174a's prompt, logged like the other plugin crashes.
    if (const auto deaths = pluginSandboxDeathCount(); deaths != sandboxDeathsSeen)
    {
        sandboxDeathsSeen = deaths;
        const auto all = pluginSandboxDeaths();
        if (! all.empty())
        {
            const auto& d = all.back();
            PluginHost::settingsFolder().getChildFile ("plugin-crashes.log").appendText (
                juce::Time::getCurrentTime().toISO8601 (true) + "  " + d.plugin + "  " + (d.crashed ? "crashed" : "stopped responding")
                + (d.bits32 ? " (32-bit bridge)" : " (separate process)") + "\n");
            announceSeparateProcessCrash (d.plugin, d.crashed, d.bits32);
        }
    }

    // issue #32 (round 174a): a plugin's own editor window faulted; LYNX closed that editor and kept running
    if (const auto crashes = PluginWindows::editorCrashCount(); crashes != editorCrashesSeen)
    {
        editorCrashesSeen = crashes;
        const auto all = PluginWindows::editorCrashes();
        if (! all.empty())
            announceEditorCrash (all.back().plugin);
    }

    auto& transport = engine.getTransport();

    if (! heldKeys.empty())
    {
        if (! juce::Process::isForegroundProcess()) // switched to another app with keys down: no key-up will come
            heldKeys.releaseAll();
        else // keys released while another window (a detached panel, a dialog) had the focus
            keyStateChanged (false);
    }

    // hint bar: a gesture's own posted hint, else the control under the mouse (while it is dragged, the
    // control that was pressed, at the point it was pressed, so its value updates live); MIDI learn and
    // recording states first
    if (! hintsFrozen)
    {
        const uitrace::Scope part ("timer: hint", false);
        ui::Hint hint;
        bool live = false;
        juce::Component* hovered = nullptr;

        if (engine.isLearningMidi())
            hint.text = tr ("Move a knob/fader on the MIDI controller to link it to the parameter...");
        else if (engine.isWaitingForInput() || engine.isCountingIn())
            hint.text = idleHint();
        else if (auto posted = ui::postedHint())
        {
            hint = *posted;
            live = true;
        }
        else if (isShowing())
        {
            const auto source = juce::Desktop::getInstance().getMainMouseSource();

            if (auto* under = source.getComponentUnderMouse())
            {
                live = source.isDragging();
                const auto at = live ? source.getLastMouseDownPosition() : source.getScreenPosition();
                hint = ui::hintFor (under, under->getLocalPoint (nullptr, at).roundToInt());
                hovered = under;
            }
        }

        if (hint.isEmpty())
        {
            hint.text = idleHint();
            live = false;
        }

        hintBar.setHint (hint, live && hint.value.isNotEmpty());
        if (hoverValues)
            updateHoverValue (hovered, hint, live);
        if (++tweakTicks >= 3) // 10 times a second
        {
            tweakTicks = 0;
            refreshTweakReadout();
        }
    }

    const auto playing = transport.playing.load();
    playButton.setToggleState (playing, juce::dontSendNotification);

    updateChordPanel();

    // CPU / memory / polyphony, a few times a second
    if (++meterTicks >= 8 && perfMeter.isVisible() && ! hintsFrozen)
    {
        const uitrace::Scope part ("timer: performance meter", false);
        meterTicks = 0;
        const auto held = (int) heldKeys.size();
        const auto notes = playing ? songstats::soundingNotes (engine.getSong(), transport.positionTicks.load()) : 0;
        perfMeter.setValues (engine.getDeviceManager().getCpuUsage(), toolbar::PerformanceMeter::processMemoryBytes(), notes + held);
        const auto rt = engine.getRtStats().snapshot();
        juce::StringArray heaviest;
        for (const auto& u : engine.getHeaviestUnits())
            if (heaviest.size() < 3 && u.load >= 0.01)
                heaviest.add (u.name + " " + juce::String (juce::roundToInt (u.load * 100.0)) + "%");
        auto* device = engine.getDeviceManager().getCurrentAudioDevice();
        perfMeter.setRealtime (rt.overBudget, rt.lateCalls, engine.getRtStats().takeWindowMaxLoad(), heaviest,
                               device != nullptr ? device->getXRunCount() : -1);
        // dropouts on Windows shared mode: suggest the interface's ASIO driver (round 160), worked out once per device type
        if (device != nullptr && (rt.overBudget > 0 || rt.lateCalls > 0) && device->getTypeName() != driverTipType)
        {
            driverTipType = device->getTypeName();
            std::vector<std::string> asio;
            for (auto* type : engine.getDeviceManager().getAvailableDeviceTypes())
                if (type->getTypeName() == "ASIO")
                    for (const auto& name : type->getDeviceNames())
                        asio.push_back (name.toStdString());
            perfMeter.setDriverTip (juce::String::fromUTF8 (prefs::suggestedAsioDriver (driverTipType.toStdString(), asio).c_str()));
        }
        // Linux: the first dropouts without real-time priority explain how to get it, once per installation (round 161)
        if (device != nullptr && ! realtimeHintChecked && (rt.overBudget > 0 || device->getXRunCount() > 0)
            && lynx::rt::status() == lynx::rt::Status::denied)
        {
            realtimeHintChecked = true;
            if (lynx::rt::shouldShowHint (PluginHost::settingsFolder().getChildFile ("realtime-hint-shown")))
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon, tr ("Real-time priority"),
                    tr ("The sound broke up, and Linux runs LYNX SHARE's audio thread at normal priority. Real-time priority makes dropouts much rarer:\n\n"
                        "- install rtkit (package \"rtkit\"; most desktops already have it), or\n"
                        "- add yourself to the \"audio\" group with an rtprio limit in /etc/security/limits.d (as for JACK),\n\n"
                        "then log out and in again. LYNX SHARE never needs root. Options > Audio settings shows the current state."));
        }
    }

    if (++midiInputTicks % 30 == 1) // the open MIDI inputs, once a second
        midiLightInputs = engine.getActiveMidiInputs();
    midiLight.update (engine.getMidiActivity(), midiLightInputs);

    // If a closed dialog left the main window without a focused component, take it back so the PC keyboard
    // keeps playing. Only while the main window itself is the active one: another LYNX window (Audio editor,
    // Quick-add palette, a plugin's editor, a detached panel) keeps its focus (issue #4: grabbing it here, 30
    // times a second, pulled the main window to the front and closed the plugins' drop-down menus).
    if (shouldReclaimKeyboardFocus())
        grabKeyboardFocus();
}

bool MainComponent::shouldReclaimKeyboardFocus()
{
    auto* peer = getPeer();
    return ! hasKeyboardFocus (true) && isShowing() && peer != nullptr && peer->isFocused() && juce::Process::isForegroundProcess()
           && juce::Component::getCurrentlyModalComponent() == nullptr && ! workspace.isDetachedWindowActive();
}

void MainComponent::updateTitle()
{
    repaint (titleArea); // the project name in the toolbar
    if (! engine.isModified()) // what is on disk now: the reference for "unused recordings" after a Discard
        savedSamples = safety::referencedSamples (engine.getSong());

    if (auto* window = getTopLevelComponent())
    {
        // a live session's state (round 169j): LIVE, or what it is waiting for
        juce::String liveState;
        if (live != nullptr && live->isActive())
        {
            using S = LiveSessionManager::State;
            const auto st = live->getState();
            liveState = " [" + (st == S::live ? tr ("LIVE") : st == S::reconnecting ? tr ("Reconnecting...") : st == S::waitingHost ? tr ("Waiting for the host...") : tr ("Connecting...")) + "]";
        }
        window->setName ((engine.isModified() ? "* " : "")
                         + (projectName.isNotEmpty() ? "LYNX SHARE - " + projectName : juce::String ("LYNX SHARE")) + importedSuffix + liveState);
    }
}

std::unique_ptr<juce::AlertWindow> MainComponent::makeConfirmDiscardWindow()
{
    auto window = std::make_unique<juce::AlertWindow> (tr ("Save changes?"), tr ("The project has unsaved changes."),
                                                       juce::MessageBoxIconType::QuestionIcon);
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Discard"), 2);
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return window;
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! engine.isModified())
    {
        offerToDeleteUnusedRecordings (true, std::move (then));
        return;
    }

    auto* window = makeConfirmDiscardWindow().release();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, then] (int result) {
                                 if (result == 1)
                                     save (false, [this, then] { offerToDeleteUnusedRecordings (true, then); }); // (not after a cancelled "save as")
                                 else if (result == 2)
                                     offerToDeleteUnusedRecordings (false, then);
                             }),
                             true);
}

juce::File MainComponent::recordingFolderFor (const juce::File& projectFile) const
{
    // FL records into the project data folder's "Audio\Recorded" (REA round 134); LYNX's data folder is the folder of
    // the project file. Unsaved projects record into the global recordings folder.
    return projectFile == juce::File() ? AudioEngine::defaultRecordingFolder()
                                       : projectFile.getParentDirectory().getChildFile (safety::recordedSubfolder);
}

namespace
{
// FL's Keep / Delete question about recorded files nothing uses (FLEngine FUN_00ed1380, REA round 134)
void askKeepOrDelete (const juce::String& message, const std::vector<juce::File>& files, std::function<void (bool)> done)
{
    juce::String list;
    for (std::size_t i = 0; i < files.size() && i < 8; ++i)
        list << "\n  " << files[i].getFileName();
    if (files.size() > 8)
        list << "\n  " << tr ("...and {0} more", { juce::String ((int) files.size() - 8) });

    auto* w = new juce::AlertWindow (tr ("Unused recordings"), message + list, juce::MessageBoxIconType::QuestionIcon);
    w->addButton (tr ("Keep"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->addButton (tr ("Delete"), 1);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([done] (int result) { done (result == 1); }), true);
}

// to the recycle bin when the system has one (FL's "Delete recorded files to recycle bin")
int removeFiles (const std::vector<juce::File>& files)
{
    int removed = 0;
    for (const auto& f : files)
        removed += f.existsAsFile() && (f.moveToTrash() || f.deleteFile());
    return removed;
}
} // namespace

void MainComponent::offerToDeleteUnusedRecordings (bool keepsCurrentState, std::function<void()> then)
{
    // the takes of this session the project on disk will not use: deleted takes, undone recordings, a project closed
    // without saving (FL: "You created ... sample file(s) that are no longer used if the project isn't saved")
    std::vector<std::string> recorded;
    for (const auto& f : engine.getSessionRecordings())
        if (f.existsAsFile())
            recorded.push_back (f.getFullPathName().toStdString());
    const auto referenced = keepsCurrentState ? safety::referencedSamples (engine.getSong()) : savedSamples;
    std::vector<juce::File> unused;
    for (const auto& path : safety::unusedRecordings (recorded, referenced))
        unused.emplace_back (juce::String::fromUTF8 (path.c_str()));

    if (unused.empty() || ! askUnusedRecordings || ! persistent)
    {
        then();
        return;
    }

    askKeepOrDelete (unused.size() == 1 ? tr ("You recorded 1 audio file that this project will not use.")
                                        : tr ("You recorded {0} audio files that this project will not use.", { juce::String ((int) unused.size()) }),
                     unused, [safe = juce::Component::SafePointer<MainComponent> (this), unused, then] (bool remove) {
                         if (safe == nullptr)
                             return;
                         if (remove)
                             removeFiles (unused);
                         safe->engine.forgetRecordings (unused); // asked once per file
                         then();
                     });
}

void MainComponent::deleteUnusedRecordingsAndSave()
{
    // FL's Tools › Macros "Delete unused files and save project", for the files this project recorded: every take in
    // its recording folder (and this session's takes elsewhere) that no channel or clip uses
    if (currentFile == juce::File())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Delete unused recordings"),
                                                tr ("Save the project first: its recordings are kept next to it, in {0}.", { juce::String (safety::recordedSubfolder) }));
        return;
    }
    std::vector<std::string> recorded;
    const auto stem = currentFile.getFileNameWithoutExtension() + "_";
    for (const auto& f : recordingFolderFor (currentFile).findChildFiles (juce::File::findFiles, false, "*.wav"))
        if (f.getFileName().startsWithIgnoreCase (stem)) // other projects in the same folder record there too
            recorded.push_back (f.getFullPathName().toStdString());
    for (const auto& f : engine.getSessionRecordings())
        if (f.existsAsFile())
            recorded.push_back (f.getFullPathName().toStdString());
    std::vector<juce::File> unused;
    for (const auto& path : safety::unusedRecordings (recorded, safety::referencedSamples (engine.getSong())))
        unused.emplace_back (juce::String::fromUTF8 (path.c_str()));

    if (unused.empty())
    {
        save (false);
        ui::postHint ({ tr ("No unused recordings. Project saved."), {}, false, false }, 4000);
        return;
    }
    askKeepOrDelete (tr ("This project has {0} unused recording(s). Deleting them cannot be undone (they go to the recycle bin).",
                         { juce::String ((int) unused.size()) }),
                     unused, [safe = juce::Component::SafePointer<MainComponent> (this), unused] (bool remove) {
                         if (safe == nullptr)
                             return;
                         if (remove)
                         {
                             const auto n = removeFiles (unused);
                             safe->engine.forgetRecordings (unused);
                             ui::postHint ({ tr ("Deleted {0} unused recording(s).", { juce::String (n) }), {}, false, false }, 4000);
                         }
                         safe->save (false);
                     });
}

namespace
{
juce::String projectStem (const juce::String& projectName)
{
    return projectName.isNotEmpty() ? juce::File::createLegalFileName (projectName) : juce::String ("untitled");
}

// the backup folder's projects with their modification times (for rotation and "revert to last backup")
std::vector<safety::BackupFile> backupFiles (const juce::File& folder)
{
    std::vector<safety::BackupFile> files;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        files.push_back ({ f.getFileNameWithoutExtension().toStdString(), f.getLastModificationTime().toMilliseconds() });
    return files;
}

// "Song (autosaved on 2026-10-20 at 14h19)" in `folder`, "_2"... when that minute is taken (FL's naming, REA round 118)
juce::File newBackupFile (const juce::File& folder, const juce::String& stem, safety::BackupKind kind)
{
    const auto now = juce::Time::getCurrentTime();
    const auto name = safety::backupName (stem.toStdString(), kind, now.getYear(), now.getMonth() + 1, now.getDayOfMonth(), now.getHours(), now.getMinutes());
    const auto unique = safety::uniqueName (name, [&folder] (const std::string& n) {
        return folder.getChildFile (juce::String::fromUTF8 (n.c_str()) + ProjectFile::extension).exists();
    });
    return folder.getChildFile (juce::String::fromUTF8 (unique.c_str()) + ProjectFile::extension);
}
} // namespace

void MainComponent::autosave()
{
    // backup copy of the open project, so work isn't lost if something crashes ("Song (autosaved on 2026-10-20 at
    // 14h19).lynx" in the backup folder, like FL's Backup folder); only the newest few of each project are kept
    // The plugins' settings are read here (they answer on the message thread); writing the file (the big part:
    // every plugin's state, encoded) runs on a thread of its own, so the timed backup no longer freezes the screen
    // (round 151: 180 ms every two minutes on a project with 71 plugins). One at a time.
    if (autosaveRunning->load())
        return;
    const auto folder = autosaveFolder();
    folder.createDirectory();
    const auto stem = projectStem (projectName);
    {
        const uitrace::Scope trace ("autosave: plugin states", false);
        engine.capturePluginStates();
    }
    auto copy = std::make_shared<Song> (engine.getSong());
    const auto file = newBackupFile (folder, stem, safety::BackupKind::autosaved);
    autosaveRunning->store (true);
    juce::Thread::launch ([copy, file, running = autosaveRunning, safe = juce::Component::SafePointer<MainComponent> (this), stem] {
        juce::String error;
        const auto ok = ProjectFile::save (*copy, file, error);
        running->store (false);
        juce::MessageManager::callAsync ([safe, ok, error, stem] {
            if (safe == nullptr)
                return;
            if (! ok)
                ui::postHint ({ tr ("Autosave failed: {0}", { error }), {}, false, false }, 6000);
            else
                safe->rotateBackups (stem);
        });
    });
}

void MainComponent::rotateBackups (const juce::String& stem)
{
    const auto folder = autosaveFolder();
    for (const auto& old : safety::backupsToRemove (backupFiles (folder), stem.toStdString(), autosaveKeep))
    {
        const auto f = folder.getChildFile (juce::String::fromUTF8 (old.c_str()) + ProjectFile::extension);
        f.setReadOnly (false); // (FL clears the read-only flag before deleting an old backup too)
        f.deleteFile();
    }
}

void MainComponent::keepOverwrittenCopy (const juce::File& file)
{
    // FL moves the file a save replaces into its Backup folder as "Song (overwritten on ... at ...)"; LYNX copies it
    // (the save itself still goes through a temporary file and a rename)
    const auto folder = autosaveFolder();
    if (! file.existsAsFile() || file.isAChildOf (folder))
        return;
    folder.createDirectory();
    const auto stem = projectStem (file.getFileNameWithoutExtension());
    if (file.copyFileTo (newBackupFile (folder, stem, safety::BackupKind::overwritten)))
        rotateBackups (stem);
}

void MainComponent::saveNewVersion()
{
    // FL's "Save new version": Song.flp -> Song_2.flp -> Song_3.flp, next to the current file
    if (currentFile == juce::File())
    {
        save (true);
        return;
    }
    const auto folder = currentFile.getParentDirectory();
    const auto name = safety::nextVersionName (currentFile.getFileNameWithoutExtension().toStdString(), [&folder] (const std::string& n) {
        return folder.getChildFile (juce::String::fromUTF8 (n.c_str()) + ProjectFile::extension).exists();
    });
    const auto file = folder.getChildFile (juce::String::fromUTF8 (name.c_str()) + ProjectFile::extension);
    juce::String error;
    engine.capturePluginStates();
    if (! ProjectFile::save (engine.getSong(), file, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
        return;
    }
    engine.markSaved();
    rememberRecent (file);
    currentFile = file;
    projectName = file.getFileNameWithoutExtension();
    importedSuffix = {};
    updateTitle();
    ui::postHint ({ tr ("Saved as a new version: {0}", { file.getFileName() }), {}, false, false }, 5000);
}

void MainComponent::revertToLastBackup()
{
    const auto stem = projectStem (projectName);
    const auto name = safety::newestBackup (backupFiles (autosaveFolder()), stem.toStdString());
    if (name.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Revert to last backup"),
                                                tr ("There is no backup of this project yet."));
        return;
    }
    const auto backup = autosaveFolder().getChildFile (juce::String::fromUTF8 (name.c_str()) + ProjectFile::extension);
    const auto when = backup.getLastModificationTime().formatted ("%Y-%m-%d %H:%M");

    auto* w = new juce::AlertWindow (tr ("Revert to last backup"),
                                     tr ("Replace the open project with its backup from {0}?\n{1}\n\nThe project file itself only changes when you save.",
                                         { when, backup.getFileName() }),
                                     juce::MessageBoxIconType::QuestionIcon);
    w->addButton (tr ("Revert"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), backup] (int result) {
                            if (safe == nullptr || result != 1)
                                return;
                            juce::String error;
                            auto song = ProjectFile::load (backup, error);
                            if (! song)
                            {
                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
                                return;
                            }
                            safe->engine.getPluginHost().applySubstitutes (*song);
                            safe->engine.stop();
                            safe->engine.rewind();
                            safe->engine.setSong (std::move (*song));
                            safe->engine.markModified(); // Save writes it back to the project's own file
                            safe->updateTitle();
                            safe->playlist.showSongAfterOpen();
                        }),
                        true);
}

sessionui::Context MainComponent::sessionContext()
{
    return { engine, currentFile, projectName, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::File& project) {
                if (safe != nullptr)
                    safe->openLynxProject (project);
            } };
}

liveui::Context MainComponent::liveContext()
{
    return { engine, *live, currentFile, [safe = juce::Component::SafePointer<MainComponent> (this)] (std::function<void()> then) {
                if (safe != nullptr)
                    safe->confirmDiscard (std::move (then));
            } };
}

void MainComponent::startLiveFromCommandLine()
{
    const auto& cli = juce::JUCEApplication::getCommandLineParameterArray();
    auto value = [&] (const char* flag) {
        const auto at = cli.indexOf (flag);
        return at >= 0 && at + 1 < cli.size() ? cli[at + 1].unquoted() : juce::String();
    };
    auto o = LiveSessionManager::Options();
    o.name = value ("--live-name").isNotEmpty() ? value ("--live-name") : liveui::myName();
    o.projectFile = currentFile;
    if (const auto relay = value ("--live-relay"); relay.isNotEmpty())
        o.relay = relay;
    // round 169j: --live-chat "text" (sent once in), --live-follow (follow the host's transport), --live-resume (the
    // host resumes its interrupted session without asking)
    if (const auto text = value ("--live-chat"); text.isNotEmpty())
        juce::Timer::callAfterDelay (9000, [safe = juce::Component::SafePointer<MainComponent> (this), text] {
            if (safe != nullptr)
            {
                safe->live->sendChat (text);
                liveui::showChat (*safe->live);
            }
        });
    if (cli.contains ("--live-follow"))
        juce::Timer::callAfterDelay (8500, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->live->setFollowTransport (true);
        });
    if (cli.contains ("--live-resume"))
    {
        o.projectFile = juce::File();
        o.autoAccept = true;
        if (! live->resumeHost (LiveSessionManager::defaultJournal(), o))
            platform::testModeLog ("live session: nothing to resume");
        return;
    }
    if (! cli.contains ("--live-host") && ! cli.contains ("--live-join"))
    {
        if (! platform::testMode())
            liveui::offerResume (liveContext());
        return;
    }
    if (const auto codeFile = value ("--live-host"); codeFile.isNotEmpty())
    {
        o.autoAccept = true; // (scripted: nobody to click "Let them in")
        live->start (o);
        auto poll = std::make_shared<std::function<void (int)>>();
        *poll = [safe = juce::Component::SafePointer<MainComponent> (this), codeFile, weak = std::weak_ptr<std::function<void (int)>> (poll)] (int left) {
            if (safe == nullptr)
                return;
            if (safe->live->getState() == LiveSessionManager::State::live)
                return (void) juce::File (codeFile).replaceWithText (safe->live->getInviteCode());
            if (safe->live->isActive() && left > 0)
                juce::Timer::callAfterDelay (200, [poll = weak.lock(), left] {
                    if (poll != nullptr)
                        (*poll) (left - 1);
                });
            else
                juce::File (codeFile).replaceWithText ("ERROR " + safe->live->getStatus());
        };
        (*poll) (150);
    }
    else if (auto code = value ("--live-join"); code.isNotEmpty())
    {
        if (code.startsWith ("@"))
            code = juce::File (code.substring (1)).loadFileAsString().trim();
        live->join (code, o);
        // --live-edit: once in, add a channel of one's own (the GUI check sees it arrive on the host)
        if (cli.contains ("--live-edit"))
            juce::Timer::callAfterDelay (8000, [safe = juce::Component::SafePointer<MainComponent> (this), name = o.name] {
                if (safe == nullptr || safe->live->getState() != LiveSessionManager::State::live)
                    return;
                safe->engine.editSong ([&] (Song& s) {
                    ChannelDef c;
                    c.name = (name + " synth").toStdString();
                    c.instrument = InstrumentKind::synth;
                    s.channels.push_back (c);
                });
            });
    }
}

void MainComponent::exportPackage()
{
    // FL's zipped loop package: the project and every sample it uses in one .zip
    const auto stem = projectStem (projectName);
    const auto start = (currentFile != juce::File() ? currentFile.getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory))
                           .getChildFile (stem + ".zip");
    fileChooser = lynx::ui::makeFileChooser (tr ("Export project with samples"), start, "*.zip");
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<MainComponent> (this), stem] (const juce::FileChooser& chooser) {
                                  auto zip = chooser.getResult();
                                  if (safe == nullptr || zip == juce::File())
                                      return;
                                  zip = zip.withFileExtension (".zip");
                                  safe->engine.capturePluginStates();
                                  ProjectPackage::Result result;
                                  juce::String error;
                                  if (! ProjectPackage::exportZip (safe->engine.getSong(), stem, zip, result, error))
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
                                      return;
                                  }
                                  auto text = tr ("{0} saved with {1} samples.", { zip.getFileName(), juce::String (result.files) });
                                  if (! result.missing.isEmpty())
                                      text << "\n\n" << tr ("Not on this PC (left out):") << "\n" << result.missing.joinIntoString ("\n");
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Export project with samples"), text);
                              });
}

void MainComponent::collectSamples()
{
    // FL's "Export project data files": copy the samples the project uses into one folder (and use the copies)
    fileChooser = lynx::ui::makeFileChooser (tr ("Copy the project's samples to..."),
                                                       currentFile != juce::File() ? currentFile.getParentDirectory()
                                                                                   : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser) {
                                  const auto folder = chooser.getResult();
                                  if (safe == nullptr || folder == juce::File())
                                      return;
                                  std::map<std::string, std::string> relink;
                                  ProjectPackage::Result result;
                                  juce::String error;
                                  if (! ProjectPackage::collectSamples (safe->engine.getSong(), folder, relink, result, error))
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not copy"), error);
                                      return;
                                  }
                                  safe->engine.editSong ([&relink] (Song& song) { applyRelocations (song, relink); });
                                  auto text = tr ("{0} samples copied to {1}. The project now uses the copies.", { juce::String (result.files), folder.getFullPathName() });
                                  if (! result.missing.isEmpty())
                                      text << "\n\n" << tr ("Not on this PC (left out):") << "\n" << result.missing.joinIntoString ("\n");
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Collect project samples"), text);
                              });
}

void MainComponent::saveToNewProjectFolder()
{
    // FL's "Save to new project folder...": <chosen name>/<name>.lynx, so the project gets its own data folder (its
    // recordings go to <name>/Audio/Recorded). This session's takes still in the global recordings folder move along
    // (copied there, the project uses the copies; the originals are then offered for deletion as unused).
    const auto start = currentFile != juce::File() ? currentFile.getParentDirectory()
                                                   : lynx::platform::documentsFolder().getChildFile ("LYNX/Projects");
    start.createDirectory();
    fileChooser = lynx::ui::makeFileChooser (tr ("Save to new project folder"),
                                                       start.getChildFile (projectName.isNotEmpty() ? juce::File::createLegalFileName (projectName) : juce::String ("untitled")),
                                                       juce::String ("*") + ProjectFile::extension);
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::saveMode, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& chooser) {
        const auto chosen = chooser.getResult();
        if (safe == nullptr || chosen == juce::File())
            return;
        const auto name = chosen.getFileNameWithoutExtension();
        auto folder = chosen.getParentDirectory().getChildFile (name);
        if (folder.exists() && ! folder.findChildFiles (juce::File::findFilesAndDirectories, false).isEmpty())
            folder = folder.getNonexistentSibling (false);
        if (! folder.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"),
                                                    tr ("Could not create the folder {0}", { folder.getFullPathName() }));
            return;
        }
        auto& self = *safe;
        const auto recorded = folder.getChildFile (safety::recordedSubfolder);
        std::map<std::string, std::string> relink;
        const auto used = safety::referencedSamples (self.engine.getSong());
        for (const auto& take : self.engine.getSessionRecordings())
            if (take.existsAsFile() && take.isAChildOf (AudioEngine::defaultRecordingFolder())
                && std::any_of (used.begin(), used.end(), [&take] (const std::string& p) { return safety::samePath (p, take.getFullPathName().toStdString()); }))
            {
                recorded.createDirectory();
                const auto copy = recorded.getNonexistentChildFile (take.getFileNameWithoutExtension(), take.getFileExtension(), false);
                if (take.copyFileTo (copy))
                    relink[take.getFullPathName().toStdString()] = copy.getFullPathName().toStdString();
            }
        if (! relink.empty())
            self.engine.editSong ([&relink] (Song& song) { applyRelocations (song, relink); });

        const auto file = folder.getChildFile (name + ProjectFile::extension);
        juce::String error;
        self.engine.capturePluginStates();
        if (! ProjectFile::save (self.engine.getSong(), file, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
            return;
        }
        self.engine.markSaved();
        self.rememberRecent (file);
        self.currentFile = file;
        self.projectName = name;
        self.importedSuffix = {};
        self.updateTitle();
        ui::postHint ({ tr ("Saved in its own folder: {0}", { folder.getFullPathName() }), {}, false, false }, 5000);
    });
}

void MainComponent::startSession()
{
    // Like FL's backup on crash: a marker exists while LYNX runs; finding one at start means the last session
    // ended without closing (crash, power loss), so the newest backup written during it is offered.
    sessionLock = std::make_unique<juce::InterProcessLock> ("LYNX-ui-session");
    if (! sessionLock->enter (0))
    {
        sessionLock.reset(); // another LYNX window is running: it owns the marker
        return;
    }
    sessionMarker = lynx::platform::settingsFile ("session-running.txt");
    if (sessionMarker.existsAsFile())
    {
        const auto previousStart = juce::Time (sessionMarker.loadFileAsString().trim().getLargeIntValue());
        juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<MainComponent> (this), previousStart] {
            if (safe != nullptr)
                safe->checkPreviousSession (previousStart);
        });
    }
    sessionMarker.getParentDirectory().createDirectory();
    sessionMarker.replaceWithText (juce::String (juce::Time::getCurrentTime().toMilliseconds()));
    // takes that were recording when LYNX stopped (crash, power loss, closed while recording): after the backup offer
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->checkInterruptedTakes();
    });
}

void MainComponent::checkInterruptedTakes()
{
    if (juce::Component::getCurrentlyModalComponent() != nullptr) // a dialog is open (the backup offer): ask later
    {
        juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->checkInterruptedTakes();
        });
        return;
    }
    std::vector<juce::File> folders { engine.getRecordingFolder() };
    const auto takes = engine.recoverInterruptedTakes (folders);
    if (takes.empty())
        return;
    juce::StringArray names;
    for (const auto& j : takes)
        names.add (juce::File (juce::String::fromUTF8 (j.wav.c_str())).getFileName());
    const auto folder = juce::File (juce::String::fromUTF8 (takes.front().wav.c_str())).getParentDirectory();
    auto* w = new juce::AlertWindow (tr ("Recordings recovered"),
                                     tr ("LYNX SHARE was recording when it stopped. These takes were saved and repaired:\n{0}\n\nPut them on the Playlist "
                                         "where they were recorded?", { names.joinIntoString ("\n") }),
                                     juce::MessageBoxIconType::InfoIcon);
    w->addButton (tr ("Put them on the Playlist"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Show the folder"), 2);
    w->addButton (tr ("Keep the files only"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), takes, folder] (int result) {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                            {
                                const auto placed = safe->engine.placeRecoveredTakes (takes);
                                ui::postHint ({ tr ("{0} recovered take(s) placed on the Playlist", { juce::String (placed) }), {}, false, false }, 5000);
                            }
                            else
                            {
                                if (result == 2)
                                    folder.startAsProcess();
                                safe->engine.forgetInterruptedTakes (takes);
                            }
                        }),
                        true);
}

void MainComponent::checkPreviousSession (juce::Time sessionStart)
{
    if (juce::Component::getCurrentlyModalComponent() != nullptr) // the setup wizard or another dialog: ask later
    {
        juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<MainComponent> (this), sessionStart] {
            if (safe != nullptr)
                safe->checkPreviousSession (sessionStart);
        });
        return;
    }
    const auto folder = autosaveFolder();
    const auto name = safety::crashRecoveryCandidate (backupFiles (folder), sessionStart.toMilliseconds());
    if (name.empty())
        return; // nothing was saved in that session
    const auto backup = folder.getChildFile (juce::String::fromUTF8 (name.c_str()) + ProjectFile::extension);

    auto* w = new juce::AlertWindow (tr ("LYNX SHARE did not close properly"),
                                     tr ("The last session ended unexpectedly. Open the newest backup it saved?\n{0}  ({1})",
                                         { backup.getFileName(), backup.getLastModificationTime().formatted ("%Y-%m-%d %H:%M") }),
                                     juce::MessageBoxIconType::WarningIcon);
    w->addButton (tr ("Open backup"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Open backup folder"), 2);
    w->addButton (tr ("Ignore"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MainComponent> (this), backup, folder] (int result) {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                                safe->confirmDiscard ([safe, backup] {
                                    if (safe != nullptr)
                                        safe->openLynxProject (backup);
                                });
                            else if (result == 2)
                                folder.startAsProcess();
                        }),
                        true);
}

void MainComponent::newProject (bool askAlways)
{
    confirmDiscard ([this, askAlways] {
        if (! askAlways && ! newProjectDialogEnabled())
        {
            startNewProject (makeNewProject(), {});
            return;
        }
        showNewProjectDialog ([safe = juce::Component::SafePointer<MainComponent> (this)] (Song song, juce::File file) {
            if (safe != nullptr)
                safe->startNewProject (std::move (song), file);
        });
    });
}

void MainComponent::startNewProject (Song song, const juce::File& file)
{
    engine.stop();
    engine.rewind();
    engine.setSong (std::move (song));
    engine.markCreatedNow(); // Project info › created (a template's own date is kept, as FL does)
    currentFile = juce::File();
    projectName = {};
    importedSuffix = {};

    if (file != juce::File())
    {
        juce::String error;
        file.getParentDirectory().createDirectory();
        if (ProjectFile::save (engine.getSong(), file, error))
        {
            engine.markSaved();
            rememberRecent (file);
            currentFile = file;
            projectName = file.getFileNameWithoutExtension();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
    }
    updateTitle();
}

// The user's choice after a plugin's editor crashed (issue #32): save under a new name now, and/or run that plugin in a
// separate process from now on (like Processing › "Always run … in a separate process": persisted, applied at once)
void MainComponent::announceEditorCrash (const juce::String& plugin)
{
    const auto offerSeparate = ! pluginSandboxEnabled() && ! pluginAlwaysSeparate (plugin) && plugin != "?";
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (tr ("Plugin crashed"))
                       .withMessage (tr ("{0} crashed. Save your project now (as a new file is safest).", { plugin }) + "\n\n"
                                     + tr ("Its window was closed so that LYNX SHARE could keep running. The plugin may not work "
                                           "properly until the project is opened again."))
                       .withButton (tr ("Save as..."));
    if (offerSeparate)
        options = options.withButton (tr ("Always run {0} in a separate process", { plugin }));
    options = options.withButton (tr ("Close"));
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this), plugin, offerSeparate] (int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->save (true);
        else if (result == 2 && offerSeparate)
        {
            safe->engine.capturePluginStates(); // the new instances start from the current settings
            setPluginAlwaysSeparate (plugin, true);
            const auto name = plugin.toStdString();
            safe->engine.editSong ([name] (Song& song) {
                for (auto& c : song.channels)
                    if (c.instrument == InstrumentKind::plugin && c.plugin.name == name)
                        c.plugin.separateProcess = true;
                for (auto& track : song.mixer)
                    for (auto& fx : track.effects)
                        if (fx.kind == EffectKind::plugin && fx.plugin.name == name)
                            fx.plugin.separateProcess = true;
            });
        }
    });
}

// Round 176a: round 174a's "Plugin crashed" prompt for a plugin whose separate process died. LYNX itself is unharmed
// (the plugin ran in its own process), so it offers the save and says how to get the plugin back.
void MainComponent::announceSeparateProcessCrash (const juce::String& plugin, bool crashed, bool bits32)
{
    const auto name = plugin.isNotEmpty() ? plugin : tr ("A plugin");
    const auto where = bits32 ? tr ("{0} crashed in the 32-bit bridge.", { name }) : tr ("{0} crashed in its separate process.", { name });
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (tr ("Plugin crashed"))
                       .withMessage ((crashed ? where : tr ("{0} stopped responding (separate process).", { name })) + "\n\n"
                                     + tr ("Only its process was affected: LYNX SHARE and your project kept running, and the plugin "
                                           "was muted. Save your project; the plugin loads again the next time you open it."))
                       .withButton (tr ("Save as..."))
                       .withButton (tr ("Close"));
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<MainComponent> (this)] (int result) {
        if (safe != nullptr && result == 1)
            safe->save (true);
    });
}

void MainComponent::saveAsTemplate()
{
    auto* w = new juce::AlertWindow (tr ("Save as template"), tr ("The template appears in File › New project... (saved in {0})", { userTemplatesFolder().getFullPathName() }),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", projectName.isNotEmpty() ? projectName : tr ("My template"));
    w->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, safe = juce::Component::SafePointer<MainComponent> (this)] (int result) {
        if (result != 1 || safe == nullptr)
            return;
        safe->engine.capturePluginStates();
        juce::String error;
        if (! saveUserTemplate (safe->engine.getSong(), w->getTextEditorContents ("name"), error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
    }), true);
}

void MainComponent::chooseFileToOpen()
{
    confirmDiscard ([this] { chooseFileToOpenNow(); });
}

void MainComponent::chooseFileToOpenNow()
{
    fileChooser = lynx::ui::makeFileChooser (tr ("Open project"), currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension + ";*.dawproj;*.flp;*.als;*.zip;*.lynxpack;*.mid;*.midi");

    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult().existsAsFile())
                                      openFile (chooser.getResult());
                              });
}

void MainComponent::openFileRequest (const juce::File& file)
{
    const juce::Component::SafePointer<MainComponent> safe (this);
    if (projectLoadRunning())
    {
        juce::Timer::callAfterDelay (500, [safe, file] {
            if (safe != nullptr)
                safe->openFileRequest (file);
        });
        return;
    }
    confirmDiscard ([safe, file] {
        if (safe != nullptr)
            safe->openFile (file);
    });
}

void MainComponent::openFile (const juce::File& file)
{
    juce::String error;
    crashguard::note (("opening project " + file.getFileName()).toStdString()); // session breadcrumb (round 111)

    if (file.hasFileExtension ("zip"))
    {
        // a project package (File › Export project with samples, or FL's zipped loop package): extracted into a
        // folder of its own, then the project inside is opened (its samples are found next to it)
        const auto project = ProjectPackage::extractZip (file, ProjectPackage::defaultExtractRoot(), error);
        if (project == juce::File())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            return;
        }
        openFile (project);
        return;
    }

    if (file.hasFileExtension (sessionpack::extension))
    {
        // Online Sessions (round 168a): a project a friend sent (opened after a summary) or a part sent back (merged)
        sessionui::openPackage (sessionContext(), file);
        return;
    }

    if (file.hasFileExtension ("mid;midi"))
    {
        auto song = MidiFileIo::loadSong (file, error);

        if (! song)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            return;
        }

        engine.stop();
        engine.rewind();
        engine.setSong (std::move (*song));
        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);

        importedSuffix = " " + tr ("(imported from MIDI)");
        updateTitle();
        return;
    }

    if (file.hasFileExtension ("flp;als")) // (a Live set takes the .flp import's path, round 175a)
    {
        if (! persistent) // (command-line modes open synchronously, without scanning)
        {
            importFlp (file);
            return;
        }
        // behind a progress panel (round 112): the project's unknown plugins get a targeted scan of just their files,
        // the samples are decoded off the message thread, the hosted plugins are created one by one with their names
        // shown; the panel's bar never sits still at 0 %
        if (projectLoadRunning())
            return;
        openFlpWithProgress (*this, engine, file, [safe = juce::Component::SafePointer<MainComponent> (this), file] (bool ok, const juce::String& error) {
            if (safe == nullptr)
                return;
            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
                return;
            }
            safe->flpImported (file);
        });
        return;
    }

    openLynxProject (file);
}

void MainComponent::importFlp (const juce::File& file)
{
    juce::String error;
    loadtrace::begin (file.getFullPathName().toStdString());
    const auto ok = engine.importFlpFile (file, error);
    loadtrace::end();
    if (! ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
        return;
    }
    flpImported (file);
}

void MainComponent::flpImported (const juce::File& file)
{
    {
        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        importedFlpFile = file;
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);
        importedSuffix = " " + (file.hasFileExtension ("als") ? tr ("(imported from .als)") : tr ("(imported from FL Studio)"));
        updateTitle();
        playlist.showSongAfterOpen();

        showMissingFiles (true);
        // FL opens Project info when the project's "Show on opening" box is on (event 0x0A, REA round 167g)
        if (engine.getSong().info.showOnOpen && platform::testMode())
            platform::testModeLog ("project info shown on open");
        else if (engine.getSong().info.showOnOpen)
            openSettings ((int) SettingsPage::info);
    }
}

void MainComponent::openLynxProject (const juce::File& file)
{
    juce::String error, savedBy;
    auto song = ProjectFile::load (file, error, &savedBy);

    if (! song)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
        return;
    }

    // missing plugins get their LYNX substitute; substitutes whose original is installed now go back to it
    engine.getPluginHost().applySubstitutes (*song);

    engine.stop();
    engine.rewind();
    engine.setSong (std::move (*song));
    currentFile = file;
    importedFlpFile = juce::File();
    projectName = file.getFileNameWithoutExtension();
    importedSuffix = {};
    rememberRecent (file);
    updateTitle();
    playlist.showSongAfterOpen();

    // pain point 37 (round 177d): a project from a newer LYNX SHARE opens, but saving it here would drop what that
    // version added, so say so once (FL warns about a project from a more recent version too)
    if (safety::savedByNewerVersion (savedBy.toStdString(), JUCE_APPLICATION_VERSION_STRING))
    {
        platform::testModeLog ("project saved by newer version " + savedBy);
        if (! platform::testMode())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Saved by a newer version"),
                                                    tr ("This project was saved by LYNX SHARE {0}, and this is {1}. What the newer version added may be "
                                                        "lost if you save it here: use File › Save as... to keep the original.",
                                                        { savedBy, JUCE_APPLICATION_VERSION_STRING }));
    }

    if (persistent)
    {
        showMissingFiles (true);
        if (engine.getSong().info.showOnOpen && platform::testMode())
            platform::testModeLog ("project info shown on open");
        else if (engine.getSong().info.showOnOpen)
            openSettings (1);
    }
}

void MainComponent::openSettings (int page)
{
    const juce::Component::SafePointer<MainComponent> safe (this);
    SettingsHooks hooks;
    hooks.browserFoldersChanged = [safe] {
        if (safe != nullptr)
            safe->browser.reloadUserFolders();
    };
    hooks.getGeneral = [safe] {
        GeneralOptions o;
        if (safe == nullptr)
            return o;
        o.language = i18n::saved();
        o.uiScale = safe->toolbarLayout.uiScale;
        o.compactMenus = theme::menus::compact();
        o.waveSpectral = ui::WaveformCache::spectral;
        o.waveStereo = ui::WaveformCache::stereo;
        o.noteNames = notenames::currentStyle.load();
        o.noteFlats = notenames::currentFlats.load();
        o.noteNamesEveryKey = notenames::everyKey.load();
        o.rackPlayheadColumn = ChannelRackComponent::playheadColumn.load();
        o.rackStepsChannelColour = ChannelRackComponent::stepsChannelColour.load();
        o.reducedEffects = skin::reducedEffects();
        o.animationRate = ui::FrameTicker::rateLimit();
        o.artMotion = skin::artMotion();
        o.gpuRendering = gpuRenderingEnabled();
        o.pluginStyle = skin::forcedStyle();
        o.splash = safe->startup.splash;
        o.startupSound = safe->startup.sound;
        o.scanAtStart = backgroundScanAtStart();
        o.typingPiano = safe->typingPiano;
        o.keepEditorsOpen = ChannelRackComponent::keepEditorsOpen;
        o.selectFirstChannelWithNotes = ChannelRackComponent::selectFirstChannelWithNotes;
        o.alternateUndo = safe->alternateUndo;
        o.rememberSeek = EditorGlobals::get().rememberSeek;
        o.stretchedClipsLoop = stretchedClipsLoop();
        o.editPatternsIn = PlaylistComponent::editPatternsIn;
        o.undoLevels = safe->engine.getUndoLevels();
        o.autosaveMinutes = safe->autosaveMinutes;
        o.autosaveKeep = safe->autosaveKeep;
        o.askUnusedRecordings = safe->askUnusedRecordings;
        o.backupWaitsForQuiet = safe->backupWaitsForQuiet;
        o.backupLongestWaitMinutes = safe->backupLongestWaitMinutes;
        o.hoverValues = safe->hoverValues;
        o.crossfadesInNewProjects = newProjectDefaults().autoCrossfades;
        o.pluginSandbox = pluginSandboxEnabled();
        o.menuEntry = appImageMenuEntryInstalled();
        o.pluginSubstitutes = pluginSubstitutesEnabled();
        o.crashReports = (int) crashreporter::consent();
        o.checkUpdates = updater::automatic();
        return o;
    };
    hooks.setGeneral = [safe] (const GeneralOptions& o) {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        if (o.uiScale != self.toolbarLayout.uiScale)
        {
            auto layout = self.toolbarLayout;
            layout.uiScale = toolbarlayout::clampScale (o.uiScale);
            self.setToolbarLayout (layout, true);
        }
        theme::menus::setCompact (o.compactMenus);
        ui::FrameTicker::rateLimit() = o.animationRate; // (the tickers read it every frame)
        if (o.reducedEffects != skin::reducedEffects() || o.pluginStyle != skin::forcedStyle() || o.artMotion != skin::artMotion())
        {
            skin::setArtMotion (o.artMotion);
            skin::setReducedEffects (o.reducedEffects);
            skin::setForcedStyle (o.pluginStyle);
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) // open editors redraw at once
                if (auto* c = juce::Desktop::getInstance().getComponent (i)) c->repaint();
        }
        if (o.noteNames != notenames::currentStyle.load() || o.noteFlats != notenames::currentFlats.load()
            || o.noteNamesEveryKey != notenames::everyKey.load())
        {
            notenames::currentStyle.store (juce::jlimit (0, 2, o.noteNames));
            notenames::currentFlats.store (o.noteFlats);
            notenames::everyKey.store (o.noteNamesEveryKey);
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) // the keyboards redraw with the new names
                if (auto* c = juce::Desktop::getInstance().getComponent (i)) c->repaint();
        }
        if (o.rackPlayheadColumn != ChannelRackComponent::playheadColumn.load()
            || o.rackStepsChannelColour != ChannelRackComponent::stepsChannelColour.load())
        {
            ChannelRackComponent::playheadColumn.store (o.rackPlayheadColumn);
            ChannelRackComponent::stepsChannelColour.store (o.rackStepsChannelColour);
            self.rack.repaint();
        }
        if (o.gpuRendering != gpuRenderingEnabled())
            setGpuRenderingEnabled (o.gpuRendering);
        if (o.waveSpectral != ui::WaveformCache::spectral || o.waveStereo != ui::WaveformCache::stereo)
        {
            ui::WaveformCache::spectral = o.waveSpectral;
            ui::WaveformCache::stereo = o.waveStereo;
            ui::WaveformCache::getInstance()->settingsChanged();
        }
        if (o.typingPiano != self.typingPiano)
            self.setTypingPiano (o.typingPiano);
        if (o.scanAtStart != backgroundScanAtStart())
            setBackgroundScanAtStart (o.scanAtStart);
        if (o.pluginSandbox != pluginSandboxEnabled())
            setPluginSandboxEnabled (o.pluginSandbox);
        if (runningAsAppImage() && o.menuEntry != appImageMenuEntryInstalled())
            setAppImageMenuEntry (o.menuEntry);
        if (o.pluginSubstitutes != pluginSubstitutesEnabled())
            setPluginSubstitutesEnabled (o.pluginSubstitutes);
        if (o.crashReports != (int) crashreporter::consent())
            crashreporter::setConsent ((crashreport::Consent) juce::jlimit (0, 2, o.crashReports));
        updater::setAutomatic (o.checkUpdates);
        ChannelRackComponent::keepEditorsOpen = o.keepEditorsOpen;
        ChannelRackComponent::selectFirstChannelWithNotes = o.selectFirstChannelWithNotes;
        self.alternateUndo = o.alternateUndo;
        EditorGlobals::get().rememberSeek = o.rememberSeek;
        if (o.stretchedClipsLoop != stretchedClipsLoop())
        {
            setStretchedClipsLoop (o.stretchedClipsLoop);
            self.repaint(); // the Playlist draws what plays
        }
        PlaylistComponent::editPatternsIn = juce::jlimit (0, 2, o.editPatternsIn);
        self.startup.splash = o.splash;
        self.startup.sound = o.startupSound;
        self.engine.setUndoLevels (o.undoLevels);
        self.autosaveMinutes = prefs::clampAutosaveMinutes (o.autosaveMinutes);
        self.autosaveKeep = prefs::clampAutosaveKeep (o.autosaveKeep);
        self.askUnusedRecordings = o.askUnusedRecordings;
        self.backupWaitsForQuiet = o.backupWaitsForQuiet;
        self.backupLongestWaitMinutes = prefs::clampBackupLongestWait (o.backupLongestWaitMinutes);
        if (self.hoverValues && ! o.hoverValues && self.hoverTipShown)
            ui::hideValueTip();
        self.hoverValues = o.hoverValues;
        self.hoverTipShown = false;
        newProjectDefaults().autoCrossfades = o.crossfadesInNewProjects;
        self.autosaveTicks = 0;
        self.saveUiSettings();
        if (o.language != i18n::saved())
            self.showLanguageChoice (o.language);
    };
    hooks.manageShortcuts = [safe] {
        if (safe != nullptr)
            safe->showShortcutManager();
    };
    hooks.managePlugins = [safe] {
        if (safe != nullptr)
            safe->showPluginManager();
    };
    showSettings (engine, (SettingsPage) juce::jlimit (0, (int) SettingsPage::theme, page), std::move (hooks)); // (Theme was clamped to MIDI)
}

void MainComponent::showPluginManager()
{
    lynx::showPluginManager (engine, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->openSettings ((int) SettingsPage::files);
    });
}

void MainComponent::showShortcutManager()
{
    lynx::showShortcutManager ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr)
            return;
        safe->loadKeyMap();
        safe->setControlHints();
        safe->menuItemsChanged();
    });
}

void MainComponent::showMissingFiles (bool afterOpening)
{
    if (afterOpening && platform::testMode()) // (LYNX_TEST_MODE: nothing waits for a click)
    {
        platform::testModeLog ("Missing files dialog after opening " + projectName);
        return;
    }
    MissingFilesInfo info;
    info.projectName = projectName;
    const auto fromFlp = importedFlpFile.existsAsFile() && currentFile == juce::File();
    const auto exists = [] (const std::string& path) { return juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())) && juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile(); };

    if (fromFlp)
    {
        // the import dropped the samples it couldn't find: its report keeps them (minus the ones located since)
        const auto& report = engine.getImportReport();
        for (const auto& m : report.missingFiles)
            info.files.push_back (m);
        info.plugins = engine.getMissingPluginDetails();
        info.autoRelocated = report.relocatedFiles;
        info.searchFolders.add (importedFlpFile.getParentDirectory().getFullPathName());
    }
    else
    {
        const auto& song = engine.getSong();
        info.files = findMissingSamples (song, exists);
        auto& host = engine.getPluginHost();
        auto addPlugin = [&info] (const PluginRef& ref, const std::string& user, const char* replacement) {
            const auto name = ref.name.empty() ? ref.id : ref.name;
            auto it = std::find_if (info.plugins.begin(), info.plugins.end(), [&] (const MissingPlugin& p) { return p.name == name; });
            if (it == info.plugins.end())
                it = info.plugins.insert (info.plugins.end(), MissingPlugin { name, {}, replacement });
            if (std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
                it->usedBy.push_back (user);
        };
        for (const auto& c : song.channels)
            if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty() && host.find (juce::String::fromUTF8 (c.plugin.id.c_str())) == nullptr)
                addPlugin (c.plugin, c.name, "Silence: the channel keeps the plugin's settings until it is installed");
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (const auto& fx : song.mixer[t].effects)
                if (fx.kind == EffectKind::plugin && ! fx.plugin.id.empty() && host.find (juce::String::fromUTF8 (fx.plugin.id.c_str())) == nullptr)
                    addPlugin (fx.plugin, song.mixer[t].name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t)) : song.mixer[t].name,
                               "Bypassed: the slot keeps the plugin's settings until it is installed");
        // played by a LYNX substitute until the original is installed
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (const auto& fx : song.mixer[t].effects)
                if (fx.isSubstitute())
                {
                    const auto name = substituteOriginalName (fx);
                    auto it = std::find_if (info.plugins.begin(), info.plugins.end(), [&] (const MissingPlugin& p) { return p.name == name; });
                    if (it == info.plugins.end())
                        it = info.plugins.insert (info.plugins.end(),
                                                  MissingPlugin { name, {}, TR_NOOP ("LYNX SHARE substitute: {0} (the original plugin is used again once it is installed). FL Studio plays a missing plugin's slot unprocessed: a substitute slot's menu › Bypass all LYNX SHARE substitutes of this project does the same"),
                                                                  effectInfo (fx.kind).name });
                    const auto user = song.mixer[t].name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t)) : song.mixer[t].name;
                    if (std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
                        it->usedBy.push_back (user);
                }
        // installed, but failed to load: they play silence / are bypassed
        for (const auto& line : host.getLoadFailures())
            if (std::none_of (info.plugins.begin(), info.plugins.end(), [&] (const MissingPlugin& p) { return p.name == line.toStdString(); }))
                info.plugins.push_back ({ line.toStdString(), {}, "Silence: the plugin is installed but did not load" });
        if (currentFile.existsAsFile())
            info.searchFolders.add (currentFile.getParentDirectory().getFullPathName());
    }

    if (afterOpening && info.files.empty() && info.plugins.empty())
    {
        if (fromFlp)
            showImportReport();
        return;
    }

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    const auto flp = fromFlp ? importedFlpFile : juce::File();
    showMissingFilesDialog (
        std::move (info),
        [safe, flp] (const std::map<std::string, std::string>& relocations) {
            if (safe == nullptr)
                return;
            if (flp != juce::File())
            {
                // FL import: imported again with the located samples (the channels become samplers, slices are read...)
                juce::String error;
                if (! safe->engine.importFlpFile (flp, error, relocations, true))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            }
            else
                safe->engine.editSong ([&relocations] (Song& s) { applyRelocations (s, relocations); });
            safe->updateTitle();
        },
        [safe, afterOpening, fromFlp] {
            if (safe != nullptr && afterOpening && fromFlp)
                safe->showImportReport();
        });
}

void MainComponent::confirmPrepareForMidiExport()
{
    // FL's text, without its "(no undo)": this edit can be undone in LYNX
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("Confirm"))
                       .withMessage (tr ("All of the channels are about to be turned into MIDI Out channels in order to allow output to a MIDI file."))
                       .withButton (tr ("OK"))
                       .withButton (tr ("Cancel"));
    juce::AlertWindow::showAsync (options, [this] (int result) {
        if (result == 1)
            engine.editSongStructural ([] (Song& s) { prepareForMidiExport (s); });
    });
}

void MainComponent::changeDeclickOfSelectedClips (int mode)
{
    const auto selected = engine.getSelectedChannels();
    if (audioClipsAmong (engine.getSong(), selected) == 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("No selection"), tr ("There are no audio clip channels selected."));
        return;
    }
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (tr ("Change declicking mode?"))
                       .withMessage (tr ("Change the declicking mode of all selected audio clip channels?"))
                       .withButton (tr ("OK"))
                       .withButton (tr ("Cancel"));
    juce::AlertWindow::showAsync (options, [this, selected, mode] (int result) {
        if (result == 1)
            engine.editSong ([&selected, mode] (Song& s) { setDeclickOfAudioClips (s, selected, mode); });
    });
}

void MainComponent::switchSmartDisableForAll()
{
    // FL's Tools › Macros › "Switch smart disable for all plugins" (FLEngine FUN_0124d430, REA round 139): every
    // effect slot of every insert is set to smart disable
    engine.editSong ([] (Song& s) {
        for (auto& track : s.mixer)
            for (auto& slot : track.effects)
                slot.smartDisable = true;
    });
}

void MainComponent::exportAudio (int format)
{
    showExportDialog (engine, currentFile.getParentDirectory(), projectName, format);
}

void MainComponent::importMidiFile()
{
    const auto folder = currentFile.existsAsFile() ? currentFile.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileChooser = lynx::ui::makeFileChooser (tr ("Import MIDI file"), folder, "*.mid;*.midi");
    juce::Component::SafePointer<MainComponent> safe (this);
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (safe == nullptr || file == juce::File())
            return;
        juce::String error;
        const auto midi = MidiFileIo::parseFile (file, error);
        if (! midi)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"), error);
            return;
        }
        static MidiFileIo::ImportOptions remembered; // FL keeps the window's choices
        showMidiImportDialog (file.getFileName(), *midi, remembered, false, [safe, file] (const MidiFileIo::ImportOptions& o) {
            remembered = o;
            if (safe != nullptr)
                safe->importMidiFile (file, o);
        });
    });
}

void MainComponent::importMidiFile (const juce::File& file, const MidiFileIo::ImportOptions& o)
{
    juce::String error;
    if (o.newProject)
    {
        confirmDiscard ([this, file, o] {
            Song song;
            juce::String err;
            if (! MidiFileIo::importInto (song, file, o, err))
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"), err);
                return;
            }
            engine.stop();
            engine.rewind();
            engine.setSong (std::move (song));
            currentFile = juce::File();
            projectName = file.getFileNameWithoutExtension();
            importedSuffix = " " + tr ("(imported from MIDI)");
            updateTitle();
        });
        return;
    }

    auto song = engine.getSong();
    if (! MidiFileIo::importInto (song, file, o, error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"), error);
        return;
    }
    engine.editSong ([&song] (Song& s) { s = std::move (song); });
    showPanel (Panel::channelRack);
}

void MainComponent::exportMidiFile()
{
    // FL's File › Export › "MIDI file...": the arrangement's notes (or the current pattern's) as a Standard MIDI File
    const auto folder = currentFile.existsAsFile() ? currentFile.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto name = projectName.isNotEmpty() ? projectName : juce::String ("LYNX SHARE");
    fileChooser = lynx::ui::makeFileChooser (tr ("Export MIDI file"), folder.getChildFile (name + ".mid"), "*.mid");
    juce::Component::SafePointer<MainComponent> safe (this);
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (! file.hasFileExtension (".mid;.midi"))
                                      file = file.withFileExtension (".mid");
                                  juce::String error;
                                  if (! MidiFileIo::saveSong (safe->engine.getSong(), file, error))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);
                                  else
                                      file.revealToUser();
                              });
}

void MainComponent::save (bool askForFile, std::function<void()> afterSaved)
{
    auto write = [this, afterSaved] (const juce::File& file) {
        juce::String error;
        engine.capturePluginStates(); // plugin state lives in the plugins; copy it before saving
        keepOverwrittenCopy (file);   // the version this save replaces goes to the backup folder (like FL)

        if (ProjectFile::save (engine.getSong(), file, error))
        {
            engine.markSaved();
            rememberRecent (file);
            currentFile = file;
            projectName = file.getFileNameWithoutExtension();
            importedSuffix = {};
            updateTitle();
            if (afterSaved)
                afterSaved();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
    };

    // a project opened from the backup folder is saved somewhere else (old backups there are deleted)
    if (! askForFile && currentFile != juce::File() && ! currentFile.isAChildOf (autosaveFolder()))
    {
        write (ProjectFile::withProjectExtension (currentFile));
        return;
    }

    const auto startFolder = currentFile.isAChildOf (autosaveFolder()) ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                                       : currentFile.getParentDirectory();
    fileChooser = lynx::ui::makeFileChooser (tr ("Save project"), startFolder, juce::String ("*") + ProjectFile::extension);

    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, write] (const juce::FileChooser& chooser) {
                                  const auto chosen = chooser.getResult();
                                  if (chosen == juce::File())
                                      return;
                                  const auto file = ProjectFile::withProjectExtension (chosen);
                                  // the dialog's overwrite warning was about the name typed, without the extension
                                  if (file != chosen && file != currentFile && file.existsAsFile())
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"),
                                                                              tr ("{0} already exists. Choose another name or folder.", { file.getFullPathName() }));
                                      return;
                                  }
                                  if (! file.isAChildOf (autosaveFolder()))
                                  {
                                      write (file);
                                      return;
                                  }
                                  // FL warns the same way: the backup folder is cleaned up as new backups arrive
                                  auto* w = new juce::AlertWindow (tr ("Save in the backup folder?"),
                                                                   tr ("This folder keeps LYNX SHARE's automatic backups and old ones are deleted as new "
                                                                       "ones arrive, so a project saved here can be lost. If you opened a backup, save it "
                                                                       "somewhere else."),
                                                                   juce::MessageBoxIconType::WarningIcon);
                                  w->addButton (tr ("Save anyway"), 1);
                                  w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
                                  w->enterModalState (true, juce::ModalCallbackFunction::create ([write, file] (int r) {
                                                          if (r == 1)
                                                              write (file);
                                                      }),
                                                      true);
                              });
}

void MainComponent::placeHintBar (juce::Rectangle<int> row)
{
    // the read-out takes the right part of the hint row once something was tweaked (2/5 of it, 170..280 px), and
    // only when the hint keeps at least 220 px
    const auto readoutWidth = juce::jlimit (170, 280, row.getWidth() * 2 / 5);
    if (tweakReadout.hasContent() && row.getWidth() - readoutWidth - 6 >= 220)
    {
        tweakReadout.setBounds (row.removeFromRight (readoutWidth));
        tweakReadout.setVisible (true);
        row.removeFromRight (6);
    }
    else
    {
        tweakReadout.setVisible (false);
        tweakReadout.setBounds ({});
    }
    hintBar.setVisible (true);
    hintBar.setBounds (row);
}

// Settings › General › "Show a control's value next to the mouse..." (round 177d, pain point 10; off by default = FL,
// whose value shows in the hint bar only): the value the hint bar shows for the control under the mouse, in the
// drag read-out's pill just above the cursor. A drag shows its own read-out, so nothing here while one runs.
void MainComponent::updateHoverValue (juce::Component* hovered, const ui::Hint& hint, bool live)
{
    if (live)
    {
        hoverTipShown = false; // (the drag's read-out owns the pill now)
        return;
    }
    const auto at = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    const auto show = hovered != nullptr && hint.value.isNotEmpty() && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()
                      && hovered->getTopLevelComponent() == getTopLevelComponent();
    if (! show)
    {
        if (hoverTipShown)
            ui::hideValueTip();
        hoverTipShown = false;
        return;
    }
    if (hoverTipShown && hint.value == hoverTipText && at == hoverTipAt)
        return;
    hoverTipShown = true;
    hoverTipText = hint.value;
    hoverTipAt = at;
    ui::showValueTip (*hovered, juce::Rectangle<int> (1, 18).withCentre (hovered->getLocalPoint (nullptr, at)), hint.value);
}

void MainComponent::refreshTweakReadout()
{
    const auto had = tweakReadout.hasContent();
    if (const auto t = engine.getLastTweaked (0))
        tweakReadout.setContent (describeAutomationTarget (engine, *t), describeTargetValue (engine, *t));
    else
        tweakReadout.setContent ({}, {});
    if (had != tweakReadout.hasContent())
        resized(); // the read-out appears (or goes, with its target)
}

juce::PopupMenu MainComponent::makeLastTweakedMenu (int which)
{
    juce::PopupMenu menu;
    const auto target = engine.getLastTweaked (which);
    if (! target)
    {
        menu.addItem (tr ("No parameter"), false, false, [] {});
        return menu;
    }

    // as FL: the parameter's name, then the items of its own menu (events, automation clip, remote control, value)
    const auto valueNow = describeTargetValue (engine, *target); // (round 162d: with its value, as the read-out shows it)
    ui::addItemHeader (menu, tr ("Parameter"), describeAutomationTarget (engine, *target) + (valueNow.isNotEmpty() ? ": " + valueNow : juce::String()),
                       juce::Colours::transparentBlack);
    const auto& song = engine.getSong();
    int clip = -1;
    for (int a = 0; a < (int) song.automations.size(); ++a)
        if (song.automations[(std::size_t) a].target == *target)
            clip = a;
    juce::ignoreUnused (clip);
    addAutomationItem (menu, engine, *target, true);
    menu.addSectionHeader (tr ("Value"));
    const auto value = engine.currentTargetValue (*target);
    menu.addItem (tr ("Copy value"), value >= 0.0f, false, [value] { lastTweakedClipboard() = value; });
    menu.addItem (tr ("Paste value"), lastTweakedClipboard() >= 0.0f, false, [this, t = *target] { setTweakedValue (t, lastTweakedClipboard()); });
    menu.addItem (tr ("Type in value..."), value >= 0.0f, false, [this, t = *target, value] {
        auto spec = ui::percentSpec (describeAutomationTarget (engine, t), value);
        ui::typeValue (*this, spec, value, [this, t] (double v) { setTweakedValue (t, (float) v); });
    });
    return menu;
}

float& MainComponent::lastTweakedClipboard()
{
    static float copied = -1.0f;
    return copied;
}

void MainComponent::setTweakedValue (const AutomationTarget& target, float value)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    auto plugin = false;
    engine.editSong ([&] (Song& s) {
        if (target.kind == AutomationTargetKind::tempo)
            clipops::followTempo (s, tempoFromValue (value)); // (issue #87: audio clips follow)
        else
            plugin = ! s.setTargetValue (target, value);
    });
    if (plugin)
    {
        if (target.kind == AutomationTargetKind::channelParam)
            if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (target.index).get()))
                p->setParameterFromUi (target.param, value);
        if (target.kind == AutomationTargetKind::effectParam)
            if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (target.index, target.slot).get()))
                p->setParameterFromUi (target.param, value);
    }
    engine.noteTweaked (target);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    // FL's menu bar order (REA round 139): FILE EDIT ADD PATTERNS VIEW OPTIONS TOOLS HELP
    return { tr ("File"), tr ("Edit"), tr ("Add"), tr ("Patterns"), tr ("View"), tr ("Options"), tr ("Tools"), tr ("Help") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int barIndex, const juce::String&)
{
    juce::PopupMenu menu;
    // the bar's position → the menu built below (0 File, 1 Edit, 2 View, 3 Patterns, 4 Options, 5 Help, 10 Add, 11 Tools)
    static constexpr int menus[] = { 0, 1, 10, 3, 2, 4, 11, 5 };
    const auto index = juce::isPositiveAndBelow (barIndex, (int) std::size (menus)) ? menus[barIndex] : 5;

    if (index == 10)
    {
        // FL's ADD menu: a channel (LYNX's instruments and the installed plugins), a pattern, the plugin tools
        menu.addSectionHeader (tr ("Channel"));
        rack.addInstrumentItems (menu, -1);
        menu.addSectionHeader (tr ("Other"));
        // FL's "Automation for last tweaked parameter" (Add_AutoLastMenu → FUN_01231100, REA round 143)
        const auto last = engine.getLastTweaked (0);
        menu.addItem (last ? tr ("Automation for last tweaked parameter ({0})", { describeAutomationTarget (engine, *last) })
                           : tr ("Automation for last tweaked parameter"),
                      last.has_value(), false, [this] {
                          if (const auto t = engine.getLastTweaked (0))
                              createAutomationClip (engine, *t);
                      });
        menu.addItem (tr ("Pattern"), [this] { newPatternButton.onClick(); });
        menu.addItem (tr ("Plugin manager..."), [this] { showPluginManager(); });
        return menu;
    }

    if (index == 11)
    {
        // FL's TOOLS menu, with the tools LYNX has (REA round 139)
        menu.addItem (tr ("Browser find") + shortcutText ("browser.search"), [this] {
            showPanel (Panel::browser);
            browser.focusSearch();
        });
        menu.addItem (tr ("One-click audio recording: arm the selected insert..."), [this] { armSelectedInsertForAudio(); });
        // FL's Tools menu has these two here; they work on the selected channel's Piano Roll (round 181b)
        menu.addItem (tr ("Riff machine..."), [this] {
            openPianoRoll();
            pianoRoll.runAction ("pianoRoll.riffMachine");
        });
        menu.addItem (tr ("Chord progression tool..."), [this] {
            openPianoRoll();
            pianoRoll.runAction ("pianoRoll.chordProgression");
        });
        menu.addItem (tr ("Plugin performance monitor..."), [this] { showPluginMonitor (engine); });
        addAudioLoggerMenu (menu); // FL's Tools menu has it just above "Last tweaked parameters"
        // FL's "Last tweaked parameters" section (LastTweaked1Menu / LastTweaked2Menu → FUN_0123d1d0, REA round 143):
        // the parameter's own menu for the last two moved controls
        menu.addSectionHeader (tr ("Last tweaked parameters"));
        menu.addSubMenu (tr ("Last tweaked"), makeLastTweakedMenu (0), engine.getLastTweaked (0).has_value());
        menu.addSubMenu (tr ("Next to last tweaked"), makeLastTweakedMenu (1), engine.getLastTweaked (1).has_value());
        menu.addSectionHeader (tr ("Macros"));
        menu.addItem (tr ("Stop sound (all notes off)") + shortcutText ("tools.stopSound"), [this] { engine.stopSound(); });
        menu.addSeparator();
        // FL's Macros order (round 181b; [DFM] mainmenu, round 168f)
        menu.addItem (tr ("Prepare for performance mode..."), [this] { prepareForPerformance(); });
        menu.addItem (tr ("Prepare for MIDI export"), [this] { confirmPrepareForMidiExport(); });
        menu.addItem (tr ("Select unused channels"), [this] {
            showPanel (Panel::channelRack);
            rack.selectUnused();
        });
        menu.addItem (tr ("Purge unused audio clips"), [this] { playlist.runAction ("playlist.purgeUnusedAudio"); });
        menu.addItem (tr ("Switch smart disable for all plugins"), [this] { switchSmartDisableForAll(); });
        menu.addItem (tr ("Lock all playlist tracks to content"), [this] { playlist.runAction ("playlist.lockAllToContent"); });
        menu.addItem (tr ("Unlock all playlist tracks from content"), [this] { playlist.runAction ("playlist.unlockAllFromContent"); });
        menu.addItem (tr ("Reset empty playlist tracks"), [this] { playlist.runAction ("playlist.resetEmptyTracks"); });
        menu.addItem (tr ("Delete unused recordings and save project..."), [this] { deleteUnusedRecordingsAndSave(); });
        {
            juce::PopupMenu declick;
            int mode = 0;
            for (const auto* name : { TR_NOOP ("Out only (no bleeding)"), TR_NOOP ("Transient (no bleeding)"), TR_NOOP ("Transient (bleeding)"),
                                      TR_NOOP ("Generic (bleeding)"), TR_NOOP ("Smooth (bleeding)"), TR_NOOP ("Crossfade (bleeding)") })
            {
                declick.addItem (tr (name), [this, mode] { changeDeclickOfSelectedClips (mode); });
                ++mode;
            }
            menu.addSubMenu (tr ("Change declicking for selected channels to"), declick);
        }
        menu.addItem (tr ("Collect project samples into a folder..."), [this] { collectSamples(); });
        return menu;
    }

    if (index == 0)
    {
        menu.addItem (tr ("New") + shortcutText ("file.new"), [this] { newProject(); });
        menu.addItem (tr ("New project..."), [this] { newProject (true); });

        juce::PopupMenu templates;
        const auto names = templateNames();
        for (int i = 0; i < (int) names.size(); ++i)
            templates.addItem (juce::String::fromUTF8 (names[(std::size_t) i].c_str()), [this, i] {
                confirmDiscard ([this, i] {
                    engine.stop();
                    engine.rewind();
                    engine.setSong (makeNewProject (i));
                    currentFile = juce::File();
                    projectName = {};
                    importedSuffix = {};
                    updateTitle();
                });
            });
        menu.addSubMenu (tr ("New from template"), templates);
        menu.addItem (tr ("Open...") + shortcutText ("file.open"), [this] { chooseFileToOpen(); });
        menu.addSeparator();

        // FL's order (REA round 139): saving, then Export, then the recent projects listed in the menu itself
        menu.addItem (tr ("Save") + shortcutText ("file.save"), [this] { save (false); });
        menu.addItem (tr ("Save as...") + shortcutText ("file.saveAs"), [this] { save (true); });
        menu.addItem (tr ("Save new version") + shortcutText ("file.saveNewVersion"), [this] { saveNewVersion(); });
        menu.addItem (tr ("Save to new project folder..."), [this] { saveToNewProjectFolder(); });
        menu.addItem (tr ("Save as template..."), [this] { saveAsTemplate(); });
        menu.addItem (tr ("Delete unused recordings and save..."), [this] { deleteUnusedRecordingsAndSave(); });
        menu.addSeparator();
        {
            // FL's File › Import (ImportMenu › ImportMIDIMenu → FileImportMIDIFileAction, REA round 143)
            juce::PopupMenu imports;
            imports.addItem (tr ("MIDI file..."), [this] { importMidiFile(); });
            imports.addItem (tr ("Video file..."), [this] { videotracks::chooseAndImport (engine, this); }); // round 174d
            menu.addSubMenu (tr ("Import"), imports);
        }
        {
            // FL's File › Export (REA round 139): one entry per format, then the formats of the last export
            juce::PopupMenu exports;
            exports.addItem (tr ("Wave file..."), [this] { exportAudio (0); });
            exports.addItem (tr ("MP3 file...") + shortcutText ("file.exportMp3"), [this] { exportAudio (1); });
            exports.addItem (tr ("OGG file..."), [this] { exportAudio (2); });
            exports.addItem (tr ("FLAC file..."), [this] { exportAudio (3); });
            exports.addItem (tr ("MIDI file...") + shortcutText ("file.exportMidi"), [this] { exportMidiFile(); });
            exports.addItem (tr ("Last exported format(s)...") + shortcutText ("file.export"), [this] { exportAudio(); });
            exports.addSeparator();
            // FL's FileExportMasterAction ("Render the project to a file and master it"; FL Cloud there, on the computer here)
            exports.addItem (tr ("Master...") + shortcutText ("file.exportMaster"), [this] { showMasterDialog (engine, currentFile.getParentDirectory(), projectName); });
            exports.addSeparator();
            // FL's ExportAllPLTracksMenu / ExportAllPLTrackGroupsMenu (FileExportPLTracks… / FileExportPLGroups…, REA round 168e)
            exports.addSubMenu (tr ("All playlist tracks"), playlistTrackExportMenu (false));
            exports.addSubMenu (tr ("All playlist track groups"), playlistTrackExportMenu (true));
            exports.addSeparator();
            exports.addItem (tr ("Project with samples (.zip)..."), [this] { exportPackage(); });
            // Export to FL Studio (round 171e, idea by Tookolld): a .flp or FL's zipped loop package
            exports.addItem (tr ("FL Studio project..."), [this] {
                engine.capturePluginStates();
                showFlStudioExportDialog (engine,
                                          currentFile != juce::File() ? currentFile.getParentDirectory()
                                                                      : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                          projectStem (projectName));
            });
            // Export to Live 12 / Live 11 (round 175a, N2): "<name> Project/<name>.als" with the samples alongside
            exports.addItem (tr ("Ableton Live set (.als)..."), [this] {
                showAlsExportDialog (engine,
                                     currentFile != juce::File() ? currentFile.getParentDirectory()
                                                                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                     projectStem (projectName));
            });
            // FL's "Project bones..." (FileExportProjectBonesAction; REA round 168e)
            exports.addItem (tr ("Project bones..."), [this] { exportProjectBones (engine, currentFile.getParentDirectory(), projectName); });
            exports.addItem (tr ("Project data files (collect the samples into a folder)..."), [this] { collectSamples(); });
            menu.addSubMenu (tr ("Export"), exports);
        }
        {
            // Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md, round 168a): a project goes to a friend as a
            // .lynxpack, his part comes back the same way
            const auto guestCopy = engine.getSong().session.isGuestCopy();
            juce::PopupMenu online;
            online.addItem (tr ("Send project..."), ! guestCopy, false, [this] { sessionui::sendProject (sessionContext()); });
            online.addItem (tr ("Send my part back..."), guestCopy, false, [this] { sessionui::sendPartBack (sessionContext()); });
            online.addItem (tr ("Merge a part..."), ! guestCopy, false, [this] { sessionui::mergePart (sessionContext()); });
            online.addSeparator();
            liveui::addMenuItems (online, liveContext());
            menu.addSubMenu (tr ("Online session"), online);
        }

        menu.addSectionHeader (tr ("Recent projects"));
        const auto shown = juce::jmin (recent.getNumFiles(), 10);
        for (int i = 0; i < shown; ++i)
        {
            const auto file = recent.getFile (i);
            juce::PopupMenu::Item item (file.getFileName());
            item.shortcutKeyDescription = file.getParentDirectory().getFileName(); // its folder on the right
            item.setEnabled (file.existsAsFile()).setAction ([this, file] { confirmDiscard ([this, file] { openFile (file); }); });
            menu.addItem (std::move (item));
        }
        if (recent.getNumFiles() > shown)
        {
            juce::PopupMenu more;
            for (int i = shown; i < recent.getNumFiles(); ++i)
            {
                const auto file = recent.getFile (i);
                more.addItem (file.getFileName() + "  (" + file.getParentDirectory().getFullPathName() + ")", file.existsAsFile(),
                              false, [this, file] { confirmDiscard ([this, file] { openFile (file); }); });
            }
            menu.addSubMenu (tr ("More recent projects"), more);
        }
        if (recent.getNumFiles() == 0)
            menu.addItem (tr ("(none)"), false, false, [] {});
        menu.addSeparator();
        menu.addItem (tr ("Revert to last backup..."), [this] { revertToLastBackup(); });
        menu.addItem (tr ("Open autosave folder"), [] {
            autosaveFolder().createDirectory();
            autosaveFolder().startAsProcess();
        });
        menu.addSeparator();
        menu.addItem (tr ("Exit"), [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }
    else if (index == 1)
    {
        // FL's Edit menu: "Undo backwards" (Ctrl+Alt+Z) above "Undo" (Ctrl+Z: undo, or redo after an undo)
        menu.addItem (tr ("Undo backwards") + shortcutText ("edit.undoBackwards"), engine.canUndo(), false, [this] { undoRedo (false); });
        menu.addItem ((alternateUndo || ! engine.canRedo() ? tr ("Undo") : tr ("Undo (redo)")) + shortcutText ("edit.undo"),
                      engine.canUndo() || engine.canRedo(), false, [this] { flUndo(); });
        menu.addItem (tr ("Redo") + shortcutText ("edit.redo"), engine.canRedo(), false, [this] { undoRedo (true); });
        menu.addItem (tr ("Undo history...") + shortcutText ("edit.history"), [this] { showHistory(); });
        menu.addItem (tr ("Audio editor..."), [this] { openAudioEditor (engine); });
        menu.addSeparator();
        menu.addItem (tr ("Plugin picker") + shortcutText ("view.pluginPicker"), [this] { togglePluginPicker(); });
        menu.addItem (tr ("Quick-add palette...") + shortcutText ("panel.quickAdd"), [this] { showQuickAdd(); });
        menu.addItem (tr ("Search the Browser") + shortcutText ("browser.search"), [this] {
            showPanel (Panel::browser);
            browser.focusSearch();
        });
    }
    else if (index == 2)
    {
        for (auto panel : panelOrder)
            menu.addItem (panelName (panel) + shortcutText (panelAction (panel)), true, workspace.isPanelVisible (panel), [this, panel] { togglePanel (panel); });

        menu.addSeparator();
        menu.addItem (tr ("Maximize or restore the Playlist") + shortcutText ("panel.maximisePlaylist"), [this] {
            workspace.toggleMaximised (Panel::playlist);
            refreshPanelButtons();
        });
        {
            juce::PopupMenu detach;
            for (auto panel : panelOrder)
                detach.addItem (panelName (panel), true, workspace.isDetached (panel), [this, panel] {
                    workspace.setDetached (panel, ! workspace.isDetached (panel));
                    refreshPanelButtons();
                });
            menu.addSubMenu (tr ("Detached panels (own window)"), detach);
        }
        menu.addSeparator();
        menu.addItem (tr ("Reset layout"), [this] {
            workspace.resetLayout();
            refreshPanelButtons();
        });
        menu.addItem (tr ("Save layout as..."), [this] { saveLayoutAs(); });
        menu.addSubMenu (tr ("Load layout"), layoutsMenu());
        menu.addItem (tr ("Bring back off-screen panels"), [this] { workspace.bringBackOffscreen(); });
        menu.addItem (tr ("Close all windows (plugins, channel settings, tools)") + shortcutText ("view.closeWindows"), [this] { runAction ("view.closeWindows"); });
        menu.addItem (tr ("Close all plugin windows") + shortcutText ("view.closePluginWindows"), [this] { runAction ("view.closePluginWindows"); });
        menu.addItem (tr ("Close all windows except the focused one") + shortcutText ("view.closeUnfocusedWindows"), [this] { runAction ("view.closeUnfocusedWindows"); });
        menu.addItem (tr ("Align all channel editors") + shortcutText ("view.alignChannelEditors"), [this] { runAction ("view.alignChannelEditors"); });
        menu.addSeparator();
        menu.addItem (tr ("Plugin database (in the Browser)") + shortcutText ("view.pluginDatabase"), [this] { runAction ("view.pluginDatabase"); });
        menu.addItem (tr ("Audio editor") + shortcutText ("view.audioEditor"), [this] { openAudioEditorForSelection(); });
        menu.addItem (tr ("Tempo tapper") + shortcutText ("view.tempoTapper"), [this] { openTempoTapper (engine); });
        menu.addItem (tr ("Sample browser (separate window)") + shortcutText ("view.sampleBrowser"), true, findToolWindow ("sampleBrowser") != nullptr,
                      [this] { toggleSampleBrowser(); });
        menu.addSeparator();
        menu.addItem (tr ("Typing keyboard to piano") + shortcutText ("view.typingPiano"), true, typingPiano, [this] { toggleTypingPiano(); });
        menu.addItem (tr ("Touch keyboard") + shortcutText ("view.touchKeyboard"), true, isTouchKeyboardOpen(), [this] { toggleTouchKeyboard (engine); });
        {
            // FL's View › Background (audit round 165z-a): the workspace image and colour of Options › Theme
            juce::PopupMenu background;
            background.addItem (tr ("Set image wallpaper..."), [] { themeui::setImageWallpaper(); });
            background.addItem (tr ("Remove image wallpaper"), ! themeui::current().image.empty(), false, [] { themeui::removeImageWallpaper(); });
            background.addItem (tr ("Set background color..."), [this] { openSettings ((int) SettingsPage::theme); });
            menu.addSubMenu (tr ("Background"), background);
        }
        menu.addSeparator();
        // FL's "Auto scrolling" (round 162, issue #9)
        menu.addItem (tr ("Playlist follows the playhead (auto scrolling)") + shortcutText ("playlist.follow"), true,
                      EditorGlobals::get().isFollowing (false), [this] { runAction ("playlist.follow"); });
        menu.addItem (tr ("Piano Roll follows the playhead (auto scrolling)") + shortcutText ("pianoRoll.follow"), true,
                      EditorGlobals::get().isFollowing (true), [this] { runAction ("pianoRoll.follow"); });
    }
    else if (index == 3)
    {
        menu = makePatternMenu();
    }
    else if (index == 4)
    {
        // the Settings window's pages (Settings › General gathers the app options that were menu items here)
        menu.addItem (tr ("Audio settings..."), [this] { openSettings ((int) SettingsPage::audio); });
        menu.addItem (tr ("MIDI settings...") + shortcutText ("view.midiSettings"), [this] { openSettings ((int) SettingsPage::midi); });
        menu.addItem (tr ("General settings..."), [this] { openSettings ((int) SettingsPage::general); });
        menu.addItem (tr ("Theme (colors, background)..."), [this] { openSettings ((int) SettingsPage::theme); });
        menu.addItem (tr ("File settings (search folders)..."), [this] { openSettings ((int) SettingsPage::files); });
        menu.addItem (tr ("Project settings (tempo, time signature)..."), [this] { openSettings ((int) SettingsPage::project); });
        menu.addItem (tr ("Project info...") + shortcutText ("view.projectInfo"), [this] { openSettings ((int) SettingsPage::info); });
        {
            juce::PopupMenu mixerMenu; // round 163: clean mixer, inserts and Master chain of new projects
            mixeroptions::addItems (mixerMenu, engine, true);
            menu.addSubMenu (tr ("Mixer"), mixerMenu);
        }
        menu.addSeparator();
        menu.addItem (tr ("Plugin manager..."), [this] { showPluginManager(); });
        const auto scanning = engine.getPluginHost().backgroundScan().running;
        menu.addItem (scanning ? tr ("Stop the plugin scan") : tr ("Scan for plugins ({0})", { platform::pluginFormatNames() }), [this, scanning] {
            if (scanning)
                engine.getPluginHost().cancelBackgroundScan();
            else
                engine.getPluginHost().startBackgroundScan(); // progress in the hint bar; LYNX stays usable
        });
        menu.addItem (tr ("Manage shortcuts..."), [this] { showShortcutManager(); });
        menu.addItem (tr ("Plugin performance monitor..."), [this] { showPluginMonitor (engine); });
        menu.addSeparator();
        menu.addItem (tr ("Record input audio"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());
        });
        // FL's Options menu, "Switches" section (TFruityLoopsMainForm DFM: KBToMIDIMenu = OptionsTypingKbToPianoAction,
        // "Typing keyboard to piano", Ctrl+T, checkable, right before Metronome; round 168t, issue #34)
        menu.addItem (tr ("Typing keyboard to piano") + shortcutText ("view.typingPiano"), true, typingPiano, [this] { toggleTypingPiano(); });
        menu.addItem (tr ("Metronome") + shortcutText ("transport.metronome"), true, engine.getTransport().metronome.load(), [this] {
            metronomeButton.setToggleState (! metronomeButton.getToggleState(), juce::sendNotification);
        });
        menu.addItem (tr ("Metronome volume ({0})...", { ui::utilityVolumeText (engine.getTransport().metronomeVolume.load()) }),
                      [this] { showMetronomeVolume(); });
        {
            // FL's Options › "Auto scrolling" (audit round 165z-a): one switch for every editor's Follow (Playlist,
            // Piano roll, Channel rack, Audio editor); each window's own Follow button still sets just that window
            auto& globals = EditorGlobals::get();
            const auto on = globals.isFollowing (false) && globals.isFollowing (true);
            menu.addItem (tr ("Auto scrolling"), true, on, [on] {
                auto& g = EditorGlobals::get();
                for (auto v : { EditorGlobals::FollowView::playlist, EditorGlobals::FollowView::pianoRoll,
                                EditorGlobals::FollowView::channelRack, EditorGlobals::FollowView::audioEditor })
                    g.setFollowingView (v, ! on);
            });
        }
        {
            juce::PopupMenu recording;
            addRecordOptions (recording);
            menu.addSubMenu (tr ("Recording"), recording);
        }
        // LYNX's own ADR (round 174d, issue #83): cues, the record cycle with streamers and beeps, takes, comp, Align
        menu.addItem (tr ("ADR (dialogue replacement)..."), [this] { adrwindow::show (engine); });
        {
            // LYNX's own (REA round 153): everything the Master plays, after the fader, goes to a WAV in the background
            auto& logger = engine.getAudioLogger();
            menu.addItem (tr ("Record the Master to a file"), true, logger.isLogging(), [this] {
                auto& log = engine.getAudioLogger();
                if (log.isLogging())
                {
                    const auto file = log.currentFile();
                    const auto seconds = log.secondsWritten();
                    log.stop();
                    ui::postHint ({ tr ("Recording of the Master saved: {0} ({1} s)", { file.getFileName(), juce::String (seconds, 1) }), {}, false, false }, 5000);
                }
                else if (! log.start (AudioLogger::defaultFolder(), engine.getTransport().sampleRate.load()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Record the Master to a file"),
                                                            tr ("Could not create a file in {0}", { AudioLogger::defaultFolder().getFullPathName() }));
                else
                    ui::postHint ({ tr ("Recording the Master to {0}", { log.currentFile().getFileName() }), {}, false, false }, 5000);
            });
            menu.addItem (tr ("Show the recordings of the Master"), [] {
                const auto folder = AudioLogger::defaultFolder();
                folder.createDirectory();
                folder.startAsProcess();
            });
        }
        {
            // FL's score logger (REA round 165f): the notes played lately, recording or not, into the current pattern
            juce::PopupMenu dumpMenu;
            for (const auto minutes : { 2, 5, 10, 20, 30 })
                dumpMenu.addItem (tr ("Last {0} minutes", { juce::String (minutes) }), [this, minutes] { dumpScoreLog (minutes); });
            menu.addSubMenu (tr ("Dump score log to selected pattern"), dumpMenu);
            menu.addItem (tr ("Clear score log"), ! engine.scoreLogEmpty(), false, [this] {
                auto options = juce::MessageBoxOptions()
                                   .withIconType (juce::MessageBoxIconType::WarningIcon)
                                   .withTitle (tr ("Score logger"))
                                   .withMessage (tr ("This will clear all logged notes!"))
                                   .withButton (tr ("Clear log"))
                                   .withButton (tr ("Cancel"));
                juce::AlertWindow::showAsync (options, [this] (int result) {
                    if (result == 1)
                        engine.clearScoreLog();
                });
            });
        }
        menu.addSubMenu (tr ("Toolbar"), makeToolbarMenu());
        menu.addSeparator();
        menu.addItem (tr ("Setup wizard (language, audio, plugin folders)..."), [this] { showSetupWizard(); });
    }
    else
    {
        menu.addItem (tr ("Shortcuts") + shortcutText ("help.shortcuts"), [this] { showHelp(); });
        menu.addItem (tr ("Missing files..."), [this] { showMissingFiles (false); });
        menu.addItem (tr ("Last FL Studio import report"), [this] {
            const auto& r = engine.getImportReport();
            if (r.converted.empty() && r.approximated.empty() && r.unsupported.empty() && r.droppedEffects.empty() && r.partialEffects.empty() && r.missingSamples == 0
                && engine.getMissingPlugins().isEmpty() && engine.getMatchedPlugins().isEmpty() && engine.getPluginsWithoutState().isEmpty()
                && engine.getPluginHost().getLoadFailures().isEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL Studio import"),
                                                        tr ("Nothing to report (no .flp imported in this session, or everything was converted)."));
            else
                showImportReport();
        });
        menu.addItem (tr ("Built-in plugins (online list)..."), [] {
            juce::URL ("https://github.com/Ka1-S/LYNX-SHARE/blob/main/docs/PLUGINS.md").launchInDefaultBrowser();
        });
        menu.addItem (tr ("Report a problem..."), [this] { crashreporter::showManualReport (&engine.getDeviceManager()); });
        if (updater::offered()) // (not in the Store build: the Store updates it)
            menu.addItem (tr ("Check for updates..."), [] { updater::checkNow(); });
        menu.addItem (tr ("About LYNX SHARE"), [] {
            auto* window = makeAboutWindow().release();
            window->enterModalState (true, nullptr, true);
            keepAboveMainWindow (*window);
        });
    }

    return menu;
}

std::unique_ptr<juce::AlertWindow> MainComponent::makeAboutWindow()
{
    struct Logo final : juce::Component
    {
        Logo() { setSize (300, 150); }
        void paint (juce::Graphics& g) override
        {
            g.drawImageWithin (logoImage(), 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
        }
    };
    struct About final : juce::AlertWindow
    {
        About()
            : juce::AlertWindow ("LYNX SHARE " JUCE_APPLICATION_VERSION_STRING,
                                 tr ("Lightweight C++/JUCE DAW with the FL Studio workflow.\n"
                                     "Independent implementation: no Image-Line code was copied."),
                                 juce::MessageBoxIconType::NoIcon)
        {
            addCustomComponent (&logo);
        }
        Logo logo;
    };
    auto window = std::make_unique<About>();
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    return std::unique_ptr<juce::AlertWindow> (window.release());
}

void MainComponent::showLanguageChoice (const juce::String& id)
{
    if (id == i18n::saved())
        return;

    i18n::save (id);
    const auto restart = [this] {
        confirmDiscard ([] {
            lynx::platform::relaunchApp(); // through the package in the Store build (platform/Package.h)
            juce::JUCEApplication::quit();
        });
    };

    // the message is shown in the language just chosen
    const auto table = i18n::tableText (id);
    const juce::LocalisedStrings strings (table, false);
    auto say = [&] (const char* text) { return table.isEmpty() ? juce::String::fromUTF8 (text) : strings.translate (juce::String::fromUTF8 (text)); };

    auto* window = new juce::AlertWindow (say (TR_NOOP ("Language")), say (TR_NOOP ("The new language is applied when LYNX SHARE restarts.")),
                                          juce::MessageBoxIconType::InfoIcon);
    window->addButton (say (TR_NOOP ("Restart now")), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (say (TR_NOOP ("Later")), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([restart] (int result) {
                                 if (result == 1)
                                     restart();
                             }),
                             true);
}

void MainComponent::rememberRecent (const juce::File& file)
{
    recent.addFile (file);
    PluginHost::settingsFolder().getChildFile ("recentes.txt").replaceWithText (recent.toString());
    menuItemsChanged();
}

std::unique_ptr<juce::Component> MainComponent::makeHelpContent (const keys::KeyMap* keyMapIn)
{
    // shortcut sheet: sections of "keys - action" rows, in two columns
    struct Row
    {
        const char* keys;
        const char* action;
    };
    struct Section
    {
        const char* title;
        std::vector<Row> rows;
    };

    static const std::vector<Section> sections {
        { TR_NOOP ("General"),
          { { TR_NOOP ("Space"), TR_NOOP ("Play / stop (back to where playback started)") },
            { "Ctrl+Space", TR_NOOP ("Play / pause") },
            { "Ctrl+Z / Ctrl+Y", TR_NOOP ("Undo / redo") },
            { "Ctrl+N / Ctrl+O", TR_NOOP ("New project / open") },
            { "Ctrl+S / Ctrl+Shift+S", TR_NOOP ("Save / save as") },
            { "Ctrl+R", TR_NOOP ("Export audio") },
            { "Enter", TR_NOOP ("Maximize or restore the Playlist") },
            { "L", TR_NOOP ("Pattern / song mode") },
            { "Ctrl+T", TR_NOOP ("Typing keyboard to piano on/off") },
            { "Z..M  Q..P", TR_NOOP ("PC keyboard as a piano (selected channels)") },
            { "Numpad +/-", TR_NOOP ("Next / previous pattern") },
            { "Ctrl+Shift+Ins", TR_NOOP ("Insert an empty pattern before the current one") } } },
        { TR_NOOP ("Workspace"),
          { { TR_NOOP ("Drag a panel title"), TR_NOOP ("Move (snaps; Alt: free)") },
            { TR_NOOP ("Double-click a title"), TR_NOOP ("Maximize / restore") },
            { TR_NOOP ("Title buttons"), TR_NOOP ("Detach into a window, maximize, close") },
            { TR_NOOP ("View menu"), TR_NOOP ("Reset, save and load layouts") } } },
        { TR_NOOP ("Channel Rack and Mixer"),
          { { TR_NOOP ("Click the name"), TR_NOOP ("Open/close the editor (Alt keeps others open)") },
            { TR_NOOP ("Ctrl+click the name"), TR_NOOP ("Audition the channel") },
            { TR_NOOP ("Selector / Up, Down"), TR_NOOP ("Select (Ctrl: add, Shift: range)") },
            { "Alt+Up / Alt+Down", TR_NOOP ("Move the selected channels") },
            { "Alt+C / Alt+Del", TR_NOOP ("Clone / delete the selected channels") },
            { "Ctrl+L", TR_NOOP ("Send the selected channels to free mixer inserts") },
            { TR_NOOP ("1..0 / Ctrl+1..0"), TR_NOOP ("Mute / solo channels 1-10 (typing piano off)") },
            { TR_NOOP ("Knobs and faders"), TR_NOOP ("Drag (Ctrl: fine), wheel, Alt+click or middle-click resets") },
            { TR_NOOP ("Right-click a knob/fader"), TR_NOOP ("Type value, reset, automation, MIDI link") },
            { TR_NOOP ("Right-click Pattern / Song"), TR_NOOP ("Show the Channel Rack / Playlist") },
            { TR_NOOP ("Double-click Stop"), TR_NOOP ("All notes off") } } },
        { "Piano Roll",
          { { TR_NOOP ("Click"), TR_NOOP ("Create a note (or chord, via the selector)") },
            { TR_NOOP ("Drag"), TR_NOOP ("Move; the right edge changes the length") },
            { TR_NOOP ("Right-click"), TR_NOOP ("Delete") },
            { TR_NOOP ("Ctrl+drag"), TR_NOOP ("Select (Ctrl+click adds to the selection)") },
            { TR_NOOP ("Shift+drag"), TR_NOOP ("Copy") },
            { "Ctrl+A/C/X/V", TR_NOOP ("Select all, copy, cut, paste") },
            { "Ctrl+B / Ctrl+Q", TR_NOOP ("Duplicate / quantize") },
            { "Alt+M / Shift+Alt+M", TR_NOOP ("Mute / unmute") },
            { "Ctrl+P / Ctrl+D", TR_NOOP ("Portamento / deselect") },
            { TR_NOOP ("Shift+arrows"), TR_NOOP ("Transpose a semitone / move by the grid (Ctrl+Up/Down: an octave)") },
            { TR_NOOP ("Alt / Ctrl+wheel"), TR_NOOP ("Disable snap / zoom") } } },
        { "Playlist",
          { { TR_NOOP ("Click"), TR_NOOP ("Paint the current pattern") },
            { TR_NOOP ("Drag on the ruler"), TR_NOOP ("Set the loop (right-click clears)") },
            { TR_NOOP ("Right-click a clip name"), TR_NOOP ("Split, make unique...") },
            { TR_NOOP ("Track LED"), TR_NOOP ("Mute the track") },
            { "Ctrl+A/C/X/V  Ctrl+B", TR_NOOP ("Edit and duplicate") } } },
    };

    // the fourth column: every F-key of the live key map (issue #23: the sheet listed no F-keys), custom keys included
    const keys::KeyMap defaults;
    const auto& map = keyMapIn != nullptr ? *keyMapIn : defaults;
    std::vector<std::pair<juce::String, juce::String>> functionKeys;
    for (const auto& b : map.functionKeyBindings())
        if (const auto* action = keys::findAction (b.actionId))
        {
            auto text = tr (action->description);
            if (b.context != keys::Context::global)
                text << " (" << tr (b.context == keys::Context::browser       ? "Browser"
                                    : b.context == keys::Context::mixer       ? "Mixer"
                                    : b.context == keys::Context::playlist    ? "Playlist"
                                    : b.context == keys::Context::pianoRoll   ? "Piano Roll"
                                                                               : "Channel Rack")
                     << ")";
            functionKeys.push_back ({ juce::String (keys::displayString (b.chord)), text });
        }

    struct Sheet final : juce::Component
    {
        explicit Sheet (std::vector<std::pair<juce::String, juce::String>> f) : fkeys (std::move (f))
        {
            setSize (1500, juce::jmax (520, 20 + 28 + (int) fkeys.size() * 22 + 20));
        }

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::bg2);
            const auto columnWidth = (getWidth() - 5 * 24) / 4;
            int column = 0, y = 20;

            for (std::size_t i = 0; i < sections.size(); ++i)
            {
                if (i == 2 || i == 3) // next column
                {
                    ++column;
                    y = 20;
                }

                const auto x = 24 + column * (columnWidth + 24);
                const auto& section = sections[i];
                g.setColour (colour::accent);
                g.fillRoundedRectangle ((float) x, (float) y + 4.0f, 3.0f, 12.0f, 1.5f);
                g.setColour (colour::textPrimary);
                g.setFont (font (type::title, true));
                g.drawText (tr (section.title), x + 10, y, columnWidth - 10, 20, juce::Justification::centredLeft);
                y += 28;

                for (const auto& row : section.rows)
                {
                    const auto keys = tr (row.keys);
                    g.setFont (font (type::small, true));
                    const auto keyWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), keys)) + 14;
                    g.setColour (colour::bg4);
                    g.fillRoundedRectangle ((float) x, (float) y + 1.0f, (float) keyWidth, 18.0f, radius::sm);
                    g.setColour (colour::textPrimary);
                    g.drawText (keys, x, y + 1, keyWidth, 18, juce::Justification::centred);
                    g.setColour (colour::textSecondary);
                    g.setFont (font (type::body));
                    g.drawFittedText (tr (row.action), x + keyWidth + 10, y, columnWidth - keyWidth - 10, 20, juce::Justification::centredLeft, 1, 0.85f);
                    y += 24;
                }

                y += 14;
            }

            // Function keys
            const auto x = 24 + 3 * (columnWidth + 24);
            y = 20;
            g.setColour (colour::accent);
            g.fillRoundedRectangle ((float) x, (float) y + 4.0f, 3.0f, 12.0f, 1.5f);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::title, true));
            g.drawText (tr ("Function keys"), x + 10, y, columnWidth - 10, 20, juce::Justification::centredLeft);
            y += 28;
            g.setFont (font (type::small, true));
            int keyWidth = 0;
            for (const auto& [keys, text] : fkeys)
                keyWidth = juce::jmax (keyWidth, juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), keys)) + 14);
            for (const auto& [keys, text] : fkeys)
            {
                g.setFont (font (type::small, true));
                g.setColour (colour::bg4);
                g.fillRoundedRectangle ((float) x, (float) y + 1.0f, (float) keyWidth, 18.0f, radius::sm);
                g.setColour (colour::textPrimary);
                g.drawText (keys, x, y + 1, keyWidth, 18, juce::Justification::centred);
                g.setColour (colour::textSecondary);
                g.setFont (font (type::body));
                g.drawFittedText (text, x + keyWidth + 10, y, columnWidth - keyWidth - 10, 20, juce::Justification::centredLeft, 1, 0.8f);
                y += 22;
            }
        }

        std::vector<std::pair<juce::String, juce::String>> fkeys;
    };

    return std::make_unique<Sheet> (std::move (functionKeys));
}

void MainComponent::showHelp()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (makeHelpContent (&keyMap).release());
    options.dialogTitle = tr ("LYNX SHARE shortcuts");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
    {
        keepAboveMainWindow (*dialog);
        helpWindow = dialog;
    }
}

void MainComponent::closeHelp()
{
    // (the key may come from the sheet itself: delete it after this call stack)
    if (helpWindow == nullptr)
        return;
    helpWindow->setVisible (false);
    juce::MessageManager::callAsync ([w = helpWindow] { delete w.getComponent(); });
    helpWindow = nullptr;
}

void MainComponent::closeUnfocusedWindows()
{
    // FL's Ctrl+F12 (CloseAllUnfocusedWindowsAction, round 167o): every window but the focused one closes. LYNX's
    // docked or detached panels and dialogs that wait for an answer stay, as for F12.
    auto* focused = juce::TopLevelWindow::getActiveTopLevelWindow();
    auto* main = getTopLevelComponent();
    juce::Array<juce::Component::SafePointer<juce::DocumentWindow>> close;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        if (auto* w = dynamic_cast<juce::DocumentWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            if (w != focused && w != main && w->isVisible() && ! w->isCurrentlyModal() && ! (bool) w->getProperties()["lynxPanel"])
                close.add (w);
    for (auto& w : close)
        if (w != nullptr)
            w->closeButtonPressed();
}

void MainComponent::showSetupWizard()
{
    // on top of the main window (round 140), so the DAW is visible behind it from the first start on
    if (setupOverlay != nullptr)
        return;
    auto safe = juce::Component::SafePointer<MainComponent> (this);
    setupOverlay = lynx::makeSetupWizardOverlay (
        engine,
        [safe] (const juce::String& language) {
            if (safe != nullptr)
                safe->showLanguageChoice (language);
        },
        [safe] {
            juce::MessageManager::callAsync ([safe] { // the overlay's own button is still running
                if (safe != nullptr)
                    safe->setupOverlay.reset();
            });
        });
    addAndMakeVisible (*setupOverlay);
    setupOverlay->setBounds (getLocalBounds());
    setupOverlay->toFront (true);
}

void MainComponent::showImportReport()
{
    // summary of what the .flp had that LYNX doesn't reproduce exactly (only when there is something to say)
    const auto& r = engine.getImportReport();
    const auto& missing = engine.getMissingPlugins();
    auto list = [] (const std::map<std::string, int>& items) {
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto& [name, count] : items)
            sorted.push_back ({ count, name });
        std::sort (sorted.rbegin(), sorted.rend());
        juce::StringArray lines;
        for (const auto& [count, name] : sorted)
            lines.add ("  " + juce::String::fromUTF8 (name.c_str()) + (count > 1 ? " (" + juce::String (count) + ")" : juce::String()));
        return lines.joinIntoString ("\n");
    };

    juce::String text;

    if (! r.approximated.empty())
        text << tr ("Approximated by the built-in synth:") << "\n" << list (r.approximated) << "\n\n";
    if (! r.unsupported.empty())
        text << tr ("No equivalent (played with a synth preset):") << "\n" << list (r.unsupported) << "\n\n";
    if (const auto& matched = engine.getMatchedPlugins(); ! matched.isEmpty())
        text << tr ("Plugins found on this PC:") << "\n  " << matched.joinIntoString ("\n  ") << "\n\n";
    if (! missing.isEmpty())
        text << tr ("Plugins not installed on this PC (or in a folder LYNX SHARE doesn't scan; Options > Setup wizard):") << "\n  "
             << missing.joinIntoString ("\n  ") << "\n\n";
    if (const auto& lost = engine.getPluginsWithoutState(); ! lost.isEmpty())
        text << tr ("Plugins found but playing their default settings (the project saved them for the other format, VST2/VST3):") << "\n  "
             << lost.joinIntoString ("\n  ") << "\n\n";
    if (const auto failed = engine.getPluginHost().getLoadFailures(); ! failed.isEmpty())
        text << tr ("Plugins installed but not loaded (silent):") << "\n  " << failed.joinIntoString ("\n  ") << "\n\n";
    if (! r.droppedEffects.empty())
        text << tr ("Effects with no equivalent (left out):") << "\n" << list (r.droppedEffects) << "\n\n";
    if (! r.partialEffects.empty())
        text << tr ("Effects kept but approximated or partly reproduced:") << "\n" << list (r.partialEffects) << "\n\n";
    if (r.missingSamples > 0)
        text << tr ("Samples missing on this machine: {0}", { juce::String (r.missingSamples) }) << "\n";
    if (r.relocatedSamples > 0)
        text << tr ("Samples found in another folder than the project says (FL Studio's folders or next to the project): {0}",
                    { juce::String (r.relocatedSamples) }) << "\n";

    if (text.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL Studio import"), text.trimEnd());
}

void MainComponent::showAudioSettings()
{
    openSettings ((int) SettingsPage::audio);
}
// --- the Channel Rack's filter box ---------------------------------------------------------------

void MainComponent::FilterBox::paint (juce::Graphics& g)
{
    using namespace theme;
    auto area = getLocalBounds().toFloat();
    g.setColour (hovered ? colour::bg3 : colour::bg1);
    g.fillRoundedRectangle (area, radius::sm);
    g.setColour (hovered ? colour::borderStrong : colour::border);
    g.drawRoundedRectangle (area.reduced (0.5f), radius::sm, 1.0f);
    // round 170e2: a funnel names the box (it showed only "All")
    icons::draw (g, Icon::filter, area.withWidth (22.0f).withSizeKeepingCentre (13.0f, 13.0f), colour::textSecondary);
    g.setColour (colour::textPrimary);
    g.setFont (font (type::small));
    g.drawText (text, getLocalBounds().withTrimmedLeft (22).withTrimmedRight (16), juce::Justification::centredLeft, true);
    icons::draw (g, Icon::chevronDown, area.removeFromRight (16.0f).reduced (3.0f).translated (-2.0f, 0.0f), colour::textSecondary);
}
// FL's Tools › Macros › Prepare for performance mode ([DECOMP] FUN_0122d010, its "Marker settings" dialog: marker count
// 4 / 8 / 16 / 32 / 64, default 16; spacing 1…128 steps, default 16 = a bar): room for a launch grid before the song,
// a marker per column, a Start marker, Performance mode on, the zone in view. FL warns it can't be undone; in LYNX it
// is one undo step.
void MainComponent::prepareForPerformance()
{
    auto* window = new juce::AlertWindow (tr ("Prepare for performance mode"),
                                          tr ("Inserts empty columns before the song for the clips to launch, a marker on each, and a Start "
                                              "marker where the song begins; Performance mode is turned on."),
                                          juce::MessageBoxIconType::NoIcon);
    window->addComboBox ("count", { "4", "8", "16", "32", "64" }, tr ("Marker count"));
    window->addComboBox ("spacing", { tr ("1 step"), tr ("2 steps"), tr ("1 beat"), tr ("2 beats"), tr ("1 bar"), tr ("2 bars"), tr ("4 bars"), tr ("8 bars") },
                         tr ("Spacing"));
    if (auto* c = window->getComboBoxComponent ("count"))
        c->setSelectedItemIndex (2, juce::dontSendNotification);
    if (auto* c = window->getComboBoxComponent ("spacing"))
        c->setSelectedItemIndex (4, juce::dontSendNotification);
    window->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result) {
                                 if (result != 1)
                                     return;
                                 const auto count = 4 << juce::jlimit (0, 4, window->getComboBoxComponent ("count")->getSelectedItemIndex());
                                 const auto steps = 1 << juce::jlimit (0, 7, window->getComboBoxComponent ("spacing")->getSelectedItemIndex());
                                 engine.editSong ([count, steps] (Song& s) { perf::prepareForPerformance (s, count, (std::int64_t) steps * (ppq / 4)); });
                                 showPanel (Panel::playlist);
                                 playlist.runAction ("playlist.zoomPerformance");
                             }),
                             true);
    keepAboveMainWindow (*window);
}

} // namespace lynx
