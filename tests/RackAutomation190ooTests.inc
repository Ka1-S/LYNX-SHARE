// Round 190oo (design review 2, PAT-7; details DE-123's blocker): FL's automation clip channels in the Channel rack
// (core/RackAutomation.h). The rows sit where FL's channel order has them (.flp import), stay in place while channels are
// inserted, removed and moved, move as FL moves a channel, and go with their automation. The .lynx round trip, the
// default of an older file and the copy for 0.1.30 are checked in the app's --selftest ("rackauto190oo": ProjectFile).

#include "core/RackAutomation.h"
#include "core/RackOps.h"

namespace ra190oo
{
lynx::Automation clip (const std::string& name, int slot = -1)
{
    lynx::Automation a;
    a.name = name;
    a.points = { { 0, 0.2f, 0.0f }, { 4 * lynx::ppq, 0.8f, 0.0f } };
    a.rackSlot = slot;
    return a;
}

lynx::Song rack (int channels)
{
    lynx::Song s;
    for (int c = 0; c < channels; ++c)
        s.channels.emplace_back().name = std::string (1, (char) ('A' + c));
    return s;
}

// the rack top to bottom as text: channels by name, automation rows by name in brackets
std::string text (const lynx::Song& s)
{
    std::string out;
    for (const auto& e : lynx::rackauto::order (s))
        out += e.automation ? "[" + s.automations[(std::size_t) e.index].name + "]" : s.channels[(std::size_t) e.index].name;
    return out;
}
} // namespace ra190oo

TEST (rackauto190oo_flp_import_keeps_fl_channel_order)
{
    using namespace lynx;
    flp::Project p;
    const auto add = [&p] (int index, flp::ChannelType type, const char* name) {
        flp::Channel c;
        c.index = index;
        c.type = type;
        c.name = name;
        if (type == flp::ChannelType::automationClip)
        {
            c.automation.push_back ({ 0.0, 0.2, 0.0f, 0 });
            c.automation.push_back ({ 4.0, 0.8, 0.0f, 0 });
        }
        p.channels.push_back (c);
    };
    // Thief's rack around the finding: YELP, three automation clips, Riser..Metal; one more clip at the very end
    add (0, flp::ChannelType::sampler, "Kick");
    add (1, flp::ChannelType::sampler, "YELP");
    add (2, flp::ChannelType::automationClip, "Volume Duck");
    add (3, flp::ChannelType::automationClip, "GROSS Saw Bass");
    add (4, flp::ChannelType::automationClip, "CRASH REVERB");
    add (5, flp::ChannelType::sampler, "Riser Noise Metal");
    add (6, flp::ChannelType::automationClip, "Last");
    const auto song = importFlp (flp::parseProject (flp::writeProject (p)), fx171e::importOptions());
    CHECK (song.channels.size() == 3 && song.automations.size() == 4);
    if (song.channels.size() != 3 || song.automations.size() != 4)
        return;
    CHECK (song.automations[0].rackSlot == 2 && song.automations[1].rackSlot == 2 && song.automations[2].rackSlot == 2);
    CHECK (song.automations[3].rackSlot == -1); // (after the last channel)
    CHECK (ra190oo::text (song) == "KickYELP[Volume Duck][GROSS Saw Bass][CRASH REVERB]Riser Noise Metal[Last]");
    const auto rows = rackauto::rows (song);
    CHECK (rackauto::rowsBefore (song, rows) == (std::vector<int> { 0, 0, 3, 4 }));
}

TEST (rackauto190oo_old_project_rows_after_the_channels)
{
    using namespace lynx;
    // a project saved before round 190oo: every clip at −1, so the channel rows keep their places and the clips follow
    auto s = ra190oo::rack (3);
    CHECK (rackauto::rows (s).empty() && rackauto::rowsBefore (s, {}) == (std::vector<int> { 0, 0, 0, 0 })); // (no clip: as before)
    s.automations.push_back (ra190oo::clip ("x"));
    s.automations.push_back (ra190oo::clip ("y"));
    CHECK (Automation {}.rackSlot == -1);
    CHECK (ra190oo::text (s) == "ABC[x][y]");
    CHECK (rackauto::rowsBefore (s, rackauto::rows (s)) == (std::vector<int> { 0, 0, 0, 2 }));
    // a slot past the end (a song that lost channels elsewhere) reads as "after the last channel"
    s.automations[0].rackSlot = 9;
    CHECK (ra190oo::text (s) == "ABC[x][y]");
}

TEST (rackauto190oo_only_clips_get_rows)
{
    using namespace lynx;
    auto s = ra190oo::rack (2);
    s.automations.push_back (ra190oo::clip ("clip", 1));
    auto live = ra190oo::clip ("peak", 1); // a Peak Controller link: no clip
    live.source.track = 1;
    s.automations.push_back (live);
    auto markers = ra190oo::clip ("tempo", 1); // the ruler's tempo markers
    markers.target.kind = AutomationTargetKind::tempo;
    markers.tempoMarkers = true;
    s.automations.push_back (markers);
    s.automations.push_back (ra190oo::clip ("lane", 1)); // an automation lane under a Playlist track
    s.playlistTracks.resize (1);
    s.playlistTracks[0].lanes.push_back ({ 3 });
    auto empty = ra190oo::clip ("empty", 1);
    empty.points.clear();
    s.automations.push_back (empty);
    CHECK (ra190oo::text (s) == "A[clip]B");
    CHECK (rackauto::listed (s, 0) && ! rackauto::listed (s, 1) && ! rackauto::listed (s, 2) && ! rackauto::listed (s, 3) && ! rackauto::listed (s, 4));
}

TEST (rackauto190oo_rows_stay_put_while_channels_change)
{
    using namespace lynx;
    auto s = ra190oo::rack (3); // A B C
    s.automations.push_back (ra190oo::clip ("x", 2)); // A B [x] C
    CHECK (ra190oo::text (s) == "AB[x]C");
    insertChannel (s, 1, ChannelDef {}); // above it
    s.channels[1].name = "N";
    CHECK (ra190oo::text (s) == "ANB[x]C");
    CHECK (s.automations[0].rackSlot == 3);
    cloneChannel (s, 2); // B's clone goes right under B, above the row (FL: a new channel under the selected one)
    CHECK (ra190oo::text (s) == "ANBB #2[x]C");
    s.removeChannel (0); // a channel above it
    CHECK (ra190oo::text (s) == "NBB #2[x]C" && s.automations[0].rackSlot == 3);
    s.removeChannel (3); // the channel under it: the row is now after the last channel
    CHECK (ra190oo::text (s) == "NBB #2[x]");
    // moving channels as a block keeps the row in its gap (the channels' order changes, the row stays between rows 2 and 3)
    auto t = ra190oo::rack (3);
    t.automations.push_back (ra190oo::clip ("x", 1)); // A [x] B C
    std::vector<int> selection { 2 };
    CHECK (moveChannels (t, selection, -1)); // C up: A [x] C B
    CHECK (ra190oo::text (t) == "A[x]CB");
}

TEST (rackauto190oo_reorder_like_fl)
{
    using namespace lynx;
    auto s = ra190oo::rack (3); // A B C
    s.automations.push_back (ra190oo::clip ("x")); // A B C [x]
    CHECK (rackauto::moveRow (s, 0, -1) && ra190oo::text (s) == "AB[x]C");
    CHECK (rackauto::moveRow (s, 0, -1) && rackauto::moveRow (s, 0, -1) && ra190oo::text (s) == "[x]ABC");
    CHECK (! rackauto::moveRow (s, 0, -1)); // (at the top)
    CHECK (rackauto::moveRow (s, 0, 1) && ra190oo::text (s) == "A[x]BC");
    CHECK (! rackauto::moveRow (s, 5, 1));

    // channels moved past the row (Alt+Up / Alt+Down, a drag): the row goes to the block's other side, the channels stay
    CHECK (rackauto::passRow (s, { 1 }, -1) && ra190oo::text (s) == "AB[x]C"); // B up past [x]
    auto t = ra190oo::rack (3);
    t.automations.push_back (ra190oo::clip ("x", 2)); // A B [x] C
    CHECK (rackauto::passRow (t, { 0, 1 }, 1) && ra190oo::text (t) == "[x]ABC"); // A B down past [x]
    CHECK (rackauto::passRow (t, { 1, 0 }, -1) && ra190oo::text (t) == "AB[x]C"); // and back up
    CHECK (rackauto::passRow (t, { 2 }, -1) && ra190oo::text (t) == "ABC[x]"); // C up past [x]
    CHECK (! rackauto::passRow (t, { 0 }, -1)); // (nothing above A: the channels move as before, moveChannels)
    CHECK (! rackauto::passRow (t, { 0 }, 1)); // (B is under A, no row)
    CHECK (rackauto::passFor (t, { 0, 2 }, -1).first == -1); // (not one block)
    CHECK (t.channels[0].name == "A" && t.channels[1].name == "B" && t.channels[2].name == "C"); // (no channel moved)
}

TEST (rackauto190oo_deleting_a_clip_removes_its_row)
{
    using namespace lynx;
    auto s = ra190oo::rack (2);
    s.automations.push_back (ra190oo::clip ("x", 1));
    s.automations.push_back (ra190oo::clip ("y", 1));
    s.automations.push_back (ra190oo::clip ("z"));
    CHECK (ra190oo::text (s) == "A[x][y]B[z]");
    s.removeAutomation (1);
    CHECK (ra190oo::text (s) == "A[x]B[z]");
    CHECK (rackauto::rows (s) == (std::vector<rackauto::Row> { { 0, 1 }, { 1, 2 } }));
}

TEST (rackauto190oo_filter_views)
{
    using namespace lynx;
    auto s = ra190oo::rack (1);
    s.channelGroups = { "Drums", rackauto::groupName };
    CHECK (rackauto::shownUnder (s, rackfilter::all));
    CHECK (! rackauto::shownUnder (s, rackfilter::unsorted) && ! rackauto::shownUnder (s, rackfilter::audioClips));
    CHECK (! rackauto::shownUnder (s, 0) && rackauto::shownUnder (s, 1)); // (FL's "Automation clips" group of an imported .flp)
}

TEST (rackauto190oo_drum_kit_split_keeps_rows_under_the_kit)
{
    using namespace lynx;
    auto s = ra190oo::rack (2); // A (the kit) B
    s.automations.push_back (ra190oo::clip ("x", 1)); // A [x] B
    std::vector<DrumPad> pads (3);
    pads[0].name = "1", pads[1].name = "2", pads[2].name = "3";
    pads[0].key = 60, pads[1].key = 61, pads[2].key = 62;
    splitDrumChannel (s, 0, pads);
    CHECK (s.channels.size() == 4 && s.automations[0].rackSlot == 3); // A - 1, A - 2, A - 3, [x], B
}
