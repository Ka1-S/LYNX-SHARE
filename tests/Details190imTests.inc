// Round 190im (details190if PLAN R5, R6, R12): the Playlist's activity reader of the meters, a deactivated grid clip and
// the two Live keys of the Clip-launch layout.
#include "core/Meters.h"
#include "core/SessionKeys.h"

TEST (details190im_meter_bank_activity_peaks_are_a_reader_of_their_own)
{
    using namespace lynx;
    MeterBank bank;
    bank.accumulate (5, 0.5f, 0.25f);
    CHECK (bank.takePeak (5, 0) == 0.5f);         // the Mixer takes its peak...
    CHECK (bank.takeViewPeak (5, 1) == 0.25f);    // ...the headers theirs...
    CHECK (bank.takeActivityPeak (5, 0) == 0.5f); // ...and the activity strips still get theirs
    CHECK (bank.takeActivityPeak (5, 1) == 0.25f);
    CHECK (bank.takeActivityPeak (5, 0) == 0.0f); // read and reset
    CHECK (bank.takeSessionPeak (5, 0) == 0.5f);  // (the Session mixer's untouched)
    CHECK (bank.takeActivityPeak (-1, 0) == 0.0f && bank.takeActivityPeak (maxMixerTracks, 0) == 0.0f);
}

TEST (details190im_deactivated_grid_clip_stays_silent)
{
    using namespace lynx;
    using namespace grid181a;
    CHECK (ClipGridSlot {}.active); // (the default: projects saved before stay active)
    auto song = patternSong();
    launcher::setClip (song, 0, 0, patternClip (0));
    launcher::setClip (song, 1, 0, patternClip (1));
    // a scene launch plays both
    Run both (song);
    both.command (launcher::Command::Type::launchScene, 0, 0);
    both.until (4.0 * bar);
    CHECK (! both.onsOf (60).empty() && ! both.onsOf (61).empty());
    // track 1's clip deactivated: the scene plays only track 0, a launch of the clip itself plays nothing
    launcher::settingsAt (*song.clipGrid, 1, 0)->active = false;
    Run scene (song);
    scene.command (launcher::Command::Type::launchScene, 0, 0);
    scene.until (4.0 * bar);
    CHECK (scene.onsOf (60) == both.onsOf (60));
    CHECK (scene.onsOf (61).empty());
    Run own (song);
    own.launch (1, 0);
    own.until (4.0 * bar);
    CHECK (own.events.empty());
    // active again: it plays
    launcher::settingsAt (*song.clipGrid, 1, 0)->active = true;
    Run again (song);
    again.launch (1, 0);
    again.until (4.0 * bar);
    CHECK (! again.onsOf (61).empty());
}

TEST (details190im_live_keys_0_and_ctrl_e_only_in_clip_launch)
{
    using namespace lynx;
    using layoutstyle::Style;
    using sessionkeys::Command;
    for (const auto c : { Command::gridDeactivateClip, Command::splitClips })
    {
        CHECK (sessionkeys::defaultKeyActs (c, Style::clipLaunch));
        CHECK (! sessionkeys::defaultKeyActs (c, Style::pattern) && ! sessionkeys::defaultKeyActs (c, Style::simple));
        CHECK (std::string (sessionkeys::menuKey (c, Style::pattern)).empty() && std::string (sessionkeys::menuKey (c, Style::simple)).empty());
    }
    CHECK (std::string (sessionkeys::menuKey (Command::gridDeactivateClip, Style::clipLaunch)) == "\t0");
    CHECK (std::string (sessionkeys::menuKey (Command::splitClips, Style::clipLaunch)) == "\tCtrl+E");
}
