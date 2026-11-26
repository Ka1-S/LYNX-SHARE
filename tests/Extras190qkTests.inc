// Round 190qk: which LYNX extras are on by default, and how a profile follows a default (core/ExtrasDefaults.h). The
// preferences' own initial values are checked against the list by --selftest's item extras190qk (the UI ones live in the
// app); here the list itself and the ui-options.txt rules.

#include "core/ExtrasDefaults.h"

TEST (extras190qk_default_lists)
{
    using namespace lynx;
    std::set<std::string_view> ids;
    for (const auto& item : extras::items)
        ids.insert (item.id);
    // every id the lists name is an extra of the page, once
    std::set<std::string_view> named;
    for (const auto id : extras::onSince0132)
        CHECK (ids.count (id) == 1 && named.insert (id).second);
    for (const auto id : extras::onBefore0132)
        CHECK (ids.count (id) == 1 && named.insert (id).second);
    for (const auto id : extras::onInFlExact)
        CHECK (ids.count (id) == 1 && extras::defaultOnBefore0132 (id)); // FL's own: on before 0.1.32 already
    CHECK (std::size (extras::onSince0132) == 12);
    // the six turned on are views only (none changes a key, click, file or sound; rollup 0.1.32 step 2 added PR #267's
    // and PR #268's veils; round 190ro took round 190qh's two Mixer views out again, below)
    for (const auto* id : { "pianoRoll.chordHighlight", "playlist.trackInserts",
                            "simple.projectName", "controlBar.liveScale", "playlist.dimUnselectedTracks", "playlist.darkenMutedTracks" })
        CHECK (extras::defaultOn (id) && ! extras::defaultOnBefore0132 (id) && ! extras::flExactOn (id));
    // rollup 0.1.32 step 3, the owner's decisions: #287's MIDI drop (FL ignores a .mid dropped there), #238's track colors
    // and round 190qi's LYNX-only plugin / Browser lists are on too; FL-exact turns them off
    for (const auto* id : { "playlist.midiDrop", "theme.tracksFromAccent", "plugins.recent", "plugins.oneRow", "browser.midiPreview",
                            "browser.tabCompletes" })
        CHECK (extras::defaultOn (id) && ! extras::defaultOnBefore0132 (id) && ! extras::flExactOn (id));
    // the ones that change a gesture, a shortcut, a file or the sound stay off
    for (const auto* id : { "playlist.wheelZones", "editing.tempTools", "transport.wToStart", "detail.windowKey", "editing.muteKeyToggles",
                            "playlist.rulerSeeksAnyTool", "playlist.rangeEdits", "playlist.headerWidth", "playlist.pointerClick",
                            "project.linkTracks", "project.clipLaunchStrips", "mixer.followPlaylist",
                            "mixer.keepLostLinks", "record.lastLoopPassPlays", "playlist.clipLanesFolded", "mixer.loudness", "midi.gamepad",
                            "polyEdit.prepareOnOpen", "layout.trackOnly",
                            "playlist.shadeFadedAudio", "general.hoverValues", // (visual, but over the paint / idle CPU gates)
                            // rollup 0.1.32 step 2: rounds 190qo, 190qr, 190ld's audio editor wheel, 190qg's Grouped toolbar
                            "file.alwaysCollect", "export.noDialog", "editing.zoomAtSongPosition", "playlist.gainHandleFollowsView",
                            "export.doneSound", "export.doneWindow", "audioEditor.wheelScrolls", "toolbar.grouped",
                            // round 190ro (EXTRAS-2.md OFF-1): 19 px off every FL Mixer fader and meter, +1.6 % a column
                            "mixer.peakValues", "mixer.correlation" })
        CHECK (! extras::defaultOn (id) && ! extras::flExactOn (id));
    // FL-exact: FL's own two on, the stretched clips' loop (issue #25) off
    CHECK (extras::flExactOn ("general.escCloses") && extras::flExactOn ("general.startPage"));
    CHECK (! extras::flExactOn ("general.stretchedClipsLoop") && extras::defaultOn ("general.stretchedClipsLoop"));
    // every extra but the game controller and the Soft look (round 190qw: kept in the theme) is kept in ui-options.txt
    for (const auto& item : extras::items)
        CHECK (extras::uiKeyOf (item).empty() == (item.id == "midi.gamepad" || item.id == "theme.softLook" || item.id == "theme.tracksFromAccent"));
    // the Soft look (round 190qw) is off by default and in FL-exact
    CHECK (! extras::defaultOn ("theme.softLook") && ! extras::flExactOn ("theme.softLook"));
}

TEST (extras190qk_user_set_list_text)
{
    using namespace lynx;
    CHECK (extras::parseUserSet ("").empty());
    CHECK ((extras::parseUserSet (" a.b , c.d,,\r") == std::set<std::string> { "a.b", "c.d" }));
    CHECK (extras::userSetText ({ "z.z", "a.a" }) == "a.a,z.z");
    CHECK (extras::parseUserSet (extras::userSetText ({ "x.y", "p.q" })) == (std::set<std::string> { "x.y", "p.q" }));
}

TEST (extras190qk_a_0131_profile_follows_the_new_defaults)
{
    using namespace lynx;
    const auto kept = extras::userSet();
    // a 0.1.31 file (no list): every extra written, the user had switched the wheel zones on and Esc off
    std::vector<std::string> lines { "typingPiano=1",     "wheelZones=1",          "shadeFadedAudio=0",  "hoverValues=0",   "prChordHighlight=0",
                                     "trackInserts=0",    "simpleProjectName=0",   "liveScaleControl=0", "escClosesWindows=0", "startPage=1",
                                     "stretchedClipsLoop=1", "tempTools=0",       "pointerClickButton=0", "pointerClick=0" };
    extras::followDefaults (lines);
    CHECK (extras::userSet() == (std::set<std::string> { "general.escCloses", "playlist.wheelZones" }));
    const std::vector<std::string> after { "typingPiano=1",     "wheelZones=1",          "shadeFadedAudio=0",  "hoverValues=0",   "prChordHighlight=1",
                                           "trackInserts=1",    "simpleProjectName=1",   "liveScaleControl=1", "escClosesWindows=0", "startPage=1",
                                           "stretchedClipsLoop=1", "tempTools=0",       "pointerClickButton=0", "pointerClick=0" };
    CHECK (lines == after);
    // written again: the list holds the two the user set (and the values the defaults)
    const std::string text = "\nwheelZones=1\nshadeFadedAudio=0\nescClosesWindows=0\ntempTools=0";
    CHECK (extras::noteUserChoices (text) == "\nextrasUserSet=general.escCloses,playlist.wheelZones");
    extras::userSet() = kept;
}

TEST (extras190qk_the_users_choice_stays)
{
    using namespace lynx;
    const auto kept = extras::userSet();
    // 0.1.32's file: the user switched the inserts column off; the chord highlight was never set
    std::vector<std::string> lines { "trackInserts=0", "prChordHighlight=0", "extrasUserSet=playlist.trackInserts" };
    extras::followDefaults (lines);
    CHECK (lines[0] == "trackInserts=0" && lines[1] == "prChordHighlight=1");
    CHECK (extras::userSet() == std::set<std::string> { "playlist.trackInserts" });
    // an empty list (after Recommended): every extra at its default
    std::vector<std::string> reset { "trackInserts=0", "wheelZones=1", "extrasUserSet=" };
    extras::followDefaults (reset);
    CHECK (reset[0] == "trackInserts=1" && reset[1] == "wheelZones=0" && extras::userSet().empty());
    // a value switched back to its default stays in the list (the user set it), one away joins it
    extras::userSet() = { "playlist.trackInserts" };
    CHECK (extras::noteUserChoices ("\ntrackInserts=1\nprChordHighlight=0\nwheelZones=0")
           == "\nextrasUserSet=pianoRoll.chordHighlight,playlist.trackInserts");
    // keys are matched whole ("pointerClick" is not "pointerClickButton")
    extras::userSet().clear();
    CHECK (extras::noteUserChoices ("\npointerClick=1\npointerClickButton=0") == "\nextrasUserSet=");
    // a new profile: no lines, nothing to change, an empty list
    std::vector<std::string> none;
    extras::followDefaults (none);
    CHECK (none.empty() && extras::userSet().empty());
    extras::userSet() = kept;
}

// Round 190ro (EXTRAS-2.md OFF-1): the Mixer's peak values and correlation bars, on in the 0.1.32 rollup's steps 2-4, are off
// again. A profile written by such a build that never switched them (not in the user-set list) follows the new default;
// one whose user switched them (in the list) keeps the choice, on or off.
TEST (extras190ro_mixer_extras_off_again)
{
    using namespace lynx;
    const auto kept = extras::userSet();
    CHECK (! extras::defaultOn ("mixer.peakValues") && ! extras::defaultOn ("mixer.correlation"));
    CHECK (! extras::defaultOnBefore0132 ("mixer.peakValues") && ! extras::flExactOn ("mixer.correlation"));
    // a rollup build's file: both written at the old default (on), the user had only set the inserts column
    std::vector<std::string> followed { "mixerPeakValues=1", "mixerCorrelation=1", "trackInserts=0", "extrasUserSet=playlist.trackInserts" };
    extras::followDefaults (followed);
    CHECK ((followed == std::vector<std::string> { "mixerPeakValues=0", "mixerCorrelation=0", "trackInserts=0", "extrasUserSet=playlist.trackInserts" }));
    // the user switched the peak values off and then on again (it stays in the list), the bars were never touched
    std::vector<std::string> chosen { "mixerPeakValues=1", "mixerCorrelation=1", "extrasUserSet=mixer.peakValues" };
    extras::followDefaults (chosen);
    CHECK (chosen[0] == "mixerPeakValues=1" && chosen[1] == "mixerCorrelation=0");
    // a 0.1.31 file has neither key: nothing to change, nothing set
    std::vector<std::string> old { "wheelZones=0" };
    extras::followDefaults (old);
    CHECK (old == std::vector<std::string> { "wheelZones=0" } && extras::userSet().empty());
    // switched on from the page later: it joins the list when written
    CHECK (extras::noteUserChoices ("\nmixerPeakValues=1\nmixerCorrelation=0") == "\nextrasUserSet=mixer.peakValues");
    extras::userSet() = kept;
}
