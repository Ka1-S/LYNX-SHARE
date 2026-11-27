// Round 190rz (0.1.32 stage Q1 part C: the rows parts A and B left "not exercised"): one test per defect found by driving
// the app (or by an FL 2026 check) and fixed.

#include "core/RackFilter.h"
#include "core/RackOps.h"

// FL 2026 (checked live, round 190rz): Ctrl+L "Route to free mixer track" skips an empty insert the user named (Insert 2
// "Bar", Insert 6 "Foo" were passed over, Insert 7 taken and named "808 Clap"), and a channel that is the only one on
// its insert keeps it (808 Kick alone on Insert 1: Insert 1 became "808 Kick").
TEST (q1c190rz_ctrl_l_skips_named_inserts_and_keeps_an_own_insert)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer = Song::defaultMixer (10);
    for (auto& c : song.channels)
        c.mixerTrack = 0;
    song.channels[0].mixerTrack = 1; // alone on Insert 1
    song.channels[1].mixerTrack = 1; // shares Insert 1 with channel 0
    song.mixer[2].name = "Bar";      // empty, but named by the user
    song.mixer[6].name = "Foo";
    CHECK (! isInsertFree (song, 2) && ! isInsertFree (song, 6) && isInsertFree (song, 3));

    // channel 1 shares its insert: it moves to the first free one (3: 2 is named)
    auto a = song;
    CHECK ((sendChannelsToFreeInserts (a, { 1 }) == std::vector<int> { 3 }));
    CHECK (a.channels[1].mixerTrack == 3 && a.mixer[3].name == a.channels[1].name && a.mixer[2].name == "Bar");

    // channel 0 after channel 1 left: alone on Insert 1, it keeps it and the insert takes its name
    CHECK ((sendChannelsToFreeInserts (a, { 0 }) == std::vector<int> { 1 }));
    CHECK (a.channels[0].mixerTrack == 1 && a.mixer[1].name == a.channels[0].name);

    // both at once, in FL's order: 0 still shares Insert 1 with 1 → 3; then 1 is alone on Insert 1 and keeps it
    auto b = song;
    CHECK ((sendChannelsToFreeInserts (b, { 0, 1 }) == std::vector<int> { 3, 1 }));

    // a channel on the Master never "keeps" it
    auto c = song;
    CHECK ((sendChannelsToFreeInserts (c, { 2 }) == std::vector<int> { 3 }));
}

// FL 2026 (checked live, round 190rz): with a group filter on, the Channel rack's selection keeps only the channels the
// filter shows (a double click on a selector = select all selected only the shown 808 Clap; switching to the group
// left the hidden channels unselected).
TEST (q1c190rz_rack_selection_keeps_the_shown_channels)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    while (song.channels.size() < 5)
        song.channels.push_back (song.channels.back());
    song.channelGroups = { "Grp" };
    song.channels[1].group = 0;
    song.channels[3].group = 0;

    std::vector<int> rows { 0, 1, 2, 3, 4 };
    int primary = 0;
    rackfilter::keepShown (song, 0, rows, primary);
    CHECK ((rows == std::vector<int> { 1, 3 }) && primary == 1);

    rows = { 0, 1, 2, 3, 4 };
    primary = 3;
    rackfilter::keepShown (song, 0, rows, primary);
    CHECK ((rows == std::vector<int> { 1, 3 }) && primary == 3);

    // "All" changes nothing
    rows = { 0, 2, 4 };
    primary = 2;
    rackfilter::keepShown (song, rackfilter::all, rows, primary);
    CHECK ((rows == std::vector<int> { 0, 2, 4 }) && primary == 2);

    // nothing shown is selected: the selection stays (the engine keeps a primary channel)
    rows = { 0, 2 };
    primary = 0;
    rackfilter::keepShown (song, 0, rows, primary);
    CHECK ((rows == std::vector<int> { 0, 2 }) && primary == 0);
}
