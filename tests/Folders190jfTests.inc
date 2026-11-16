// Round 190jf, slice B17 (community complaint #31 "nested groups"; OD1): Playlist folders = FL's track groups with LYNX's
// nesting (PlaylistTrack::nest), FL's group mute / solo (Alt+click / Alt+right-click on the LED), FL's "Hide collapsed
// grouped tracks", Alt+drag of a header into / out of a folder, nested groups from a Live set, and a folder's bus.

#include "core/ArrangeOps.h"
#include "core/FolderDrag.h"
#include "core/NoteEdit.h"
#include "core/PlaylistTrackOps.h"

namespace
{
// tracks named "0".."n-1" with depths `d` (0 = not grouped)
lynx::Song folderSong (const std::vector<int>& d)
{
    lynx::Song s;
    s.playlistTracks.resize (d.size());
    for (std::size_t i = 0; i < d.size(); ++i)
    {
        s.playlistTracks[i].name = std::to_string (i);
        s.playlistTracks[i].grouped = d[i] > 0;
        s.playlistTracks[i].nest = d[i] > 0 ? d[i] - 1 : 0;
    }
    return s;
}
std::vector<int> depthsOf (const lynx::Song& s)
{
    std::vector<int> out;
    for (int t = 0; t < (int) s.playlistTracks.size(); ++t)
        out.push_back (lynx::noteedit::folderDepth (s, t));
    return out;
}
std::string namesOf (const lynx::Song& s)
{
    std::string out;
    for (const auto& t : s.playlistTracks)
        out += t.name + ",";
    return out;
}
} // namespace

TEST (folders190jf_flat_groups_behave_as_before)
{
    using namespace lynx;
    // FL's groups: 0 header of 1,2; 3 alone; 4 header of 5
    auto s = folderSong ({ 0, 1, 1, 0, 0, 1 });
    CHECK (noteedit::isGroupHeader (s, 0) && ! noteedit::isGroupHeader (s, 1) && ! noteedit::isGroupHeader (s, 3) && noteedit::isGroupHeader (s, 4));
    CHECK (noteedit::groupHeaderOf (s, 2) == 0 && noteedit::groupHeaderOf (s, 3) == -1 && noteedit::groupHeaderOf (s, 5) == 4);
    CHECK ((noteedit::groupMembers (s, 0) == std::vector<int> { 1, 2 }) && noteedit::groupMembers (s, 1).empty());
    CHECK (noteedit::folderEnd (s, 0) == 2 && noteedit::folderEnd (s, 3) == 3);
    CHECK (! noteedit::isFoldedAway (s, 1));
    noteedit::setGroupCollapsed (s, 2, true);
    CHECK (s.playlistTracks[0].collapsed && noteedit::isFoldedAway (s, 1) && noteedit::isFoldedAway (s, 2) && ! noteedit::isFoldedAway (s, 0));
    const auto g = arrange::groupOf (s, 1);
    CHECK (g.header == 0 && g.last == 2);
    CHECK (! arrange::groupOf (s, 3).valid());
    CHECK (! noteedit::normalizeFolders (s));
    noteedit::groupWithAbove (s, 3);
    CHECK ((depthsOf (s) == std::vector<int> { 0, 1, 1, 1, 0, 1 }));
    noteedit::ungroupTrack (s, 1); // (a member leaves: 2 and 3 now follow the ungrouped 1)
    CHECK ((depthsOf (s) == std::vector<int> { 0, 0, 1, 1, 0, 1 }) && noteedit::groupHeaderOf (s, 2) == 1);
}

TEST (folders190jf_nested_folders)
{
    using namespace lynx;
    //            0  1  2  3  4  5  6
    auto s = folderSong ({ 0, 1, 2, 2, 1, 0, 1 });
    CHECK (noteedit::isGroupHeader (s, 0) && noteedit::isGroupHeader (s, 1) && ! noteedit::isGroupHeader (s, 2));
    CHECK (noteedit::folderParent (s, 2) == 1 && noteedit::folderParent (s, 4) == 0 && noteedit::folderParent (s, 1) == 0 && noteedit::folderParent (s, 5) == -1);
    CHECK (noteedit::folderEnd (s, 0) == 4 && noteedit::folderEnd (s, 1) == 3);
    CHECK ((noteedit::folderChildren (s, 0) == std::vector<int> { 1, 4 }) && (noteedit::folderChildren (s, 1) == std::vector<int> { 2, 3 }));
    CHECK ((noteedit::groupMembers (s, 0) == std::vector<int> { 1, 2, 3, 4 }) && (noteedit::groupMembers (s, 1) == std::vector<int> { 2, 3 }));
    CHECK (noteedit::groupHeaderOf (s, 1) == 1 && noteedit::groupHeaderOf (s, 3) == 1 && noteedit::groupHeaderOf (s, 4) == 0);
    // collapsing the inner folder hides 2, 3 only; the outer one hides everything under it
    noteedit::setGroupCollapsed (s, 1, true);
    CHECK (noteedit::isFoldedAway (s, 2) && noteedit::isFoldedAway (s, 3) && ! noteedit::isFoldedAway (s, 1) && ! noteedit::isFoldedAway (s, 4));
    s.playlistTracks[1].collapsed = false;
    s.playlistTracks[0].collapsed = true;
    CHECK (noteedit::isFoldedAway (s, 1) && noteedit::isFoldedAway (s, 3) && noteedit::isFoldedAway (s, 4) && ! noteedit::isFoldedAway (s, 5));
    // the inner folder's group: header 1 to 3
    const auto g = arrange::groupOf (s, 2);
    CHECK (g.header == 1 && g.last == 3);
    // Collapse all: every header, nested ones too
    pltracks::setAllGroupsCollapsed (s, true);
    CHECK (s.playlistTracks[0].collapsed && s.playlistTracks[1].collapsed && s.playlistTracks[5].collapsed && ! s.playlistTracks[2].collapsed);
}

TEST (folders190jf_normalize_and_nest)
{
    using namespace lynx;
    auto s = folderSong ({ 0, 3, 1, 4 }); // jumps: 1 can be at most 1 deep, 3 at most 2
    CHECK (noteedit::normalizeFolders (s));
    CHECK ((depthsOf (s) == std::vector<int> { 0, 1, 1, 2 }));
    s.playlistTracks[0].grouped = true; // (the first track is never grouped: folderDepth ignores it)
    CHECK (noteedit::folderDepth (s, 0) == 0);

    auto n = folderSong ({ 0, 1, 1, 2, 0 });
    CHECK (noteedit::nestTrack (n, 2)); // 2 (with its child 3) under 1
    CHECK ((depthsOf (n) == std::vector<int> { 0, 1, 2, 3, 0 }));
    CHECK (! noteedit::nestTrack (n, 2)); // already one under the track above
    CHECK (! noteedit::nestTrack (n, 0));
    // out of its folder: 2 (and 3) go below folder 1, one level up
    auto u = folderSong ({ 0, 1, 2, 2, 0 });
    CHECK (noteedit::unnestTrack (u, 2));
    CHECK (namesOf (u) == "0,1,3,2,4," && (depthsOf (u) == std::vector<int> { 0, 1, 2, 1, 0 }));
    CHECK (! noteedit::unnestTrack (u, 4));
}

TEST (folders190jf_move_folder_keeps_clips)
{
    using namespace lynx;
    auto s = folderSong ({ 0, 1, 1, 0, 0 });
    for (int t = 0; t < 5; ++t)
    {
        Clip c;
        c.track = t;
        c.start = t * ppq;
        c.length = ppq;
        s.playlist.push_back (c);
    }
    // the folder 0..2 to the end, out of any group
    CHECK (noteedit::moveFolder (s, 0, 5, 0) == 2);
    CHECK (namesOf (s) == "3,4,0,1,2," && (depthsOf (s) == std::vector<int> { 0, 0, 0, 1, 1 }));
    for (const auto& c : s.playlist) // each clip on its old track's new row
        CHECK (s.playlistTracks[(std::size_t) c.track].name == std::to_string (c.start / ppq));
    // track 4 ("4", row 1) into the folder, between 0 and 1, level 1
    CHECK (noteedit::moveFolder (s, 1, 3, 1) == 2);
    CHECK (namesOf (s) == "3,0,4,1,2," && (depthsOf (s) == std::vector<int> { 0, 0, 1, 1, 1 }));
    // the depth is clamped to what the place allows (at most one under the track above)
    CHECK (noteedit::moveFolder (s, 0, 0, 5) == 0 && noteedit::folderDepth (s, 0) == 0);
    // dropped inside itself: the place stays, the level changes
    auto t = folderSong ({ 0, 1, 1, 1 });
    CHECK (noteedit::moveFolder (t, 2, 3, 2) == 2 && (depthsOf (t) == std::vector<int> { 0, 1, 2, 1 }));
    CHECK (noteedit::moveFolder (t, 9, 0, 0) == -1);
}

TEST (folders190jf_fl_group_mute_and_solo)
{
    using namespace lynx;
    auto s = folderSong ({ 0, 1, 1, 0, 0 });
    // FL: Alt+click the LED of any member: the group (header included) takes the clicked track's flipped state
    CHECK ((noteedit::groupMuteSet (s, 2) == std::vector<int> { 0, 1, 2 }) && noteedit::groupMuteSet (s, 3).empty());
    CHECK (noteedit::muteGroup (s, 2));
    CHECK (s.playlistTracks[0].muted && s.playlistTracks[1].muted && s.playlistTracks[2].muted && ! s.playlistTracks[3].muted);
    s.playlistTracks[1].muted = false;
    CHECK (noteedit::muteGroup (s, 1)); // 1 was on: everything mutes
    CHECK (s.playlistTracks[0].muted && s.playlistTracks[1].muted && s.playlistTracks[2].muted);
    CHECK (noteedit::muteGroup (s, 0)); // 0 was muted: everything plays
    CHECK (! s.playlistTracks[0].muted && ! s.playlistTracks[1].muted && ! s.playlistTracks[2].muted);
    CHECK (! noteedit::muteGroup (s, 4));

    // Alt+right-click: the group plays alone; again: everything plays (or, with restore, what played before)
    s.playlistTracks[4].muted = true;
    flsettings::SoloMemory memory;
    CHECK (noteedit::soloGroup (s, 1, 5, &memory, true));
    CHECK (! s.playlistTracks[0].muted && ! s.playlistTracks[2].muted && s.playlistTracks[3].muted && s.playlistTracks[4].muted);
    CHECK (noteedit::soloGroup (s, 1, 5, &memory, true));
    CHECK (! s.playlistTracks[3].muted && s.playlistTracks[4].muted); // restored
    CHECK (noteedit::soloGroup (s, 0, 5));
    CHECK (noteedit::soloGroup (s, 0, 5));
    CHECK (! s.playlistTracks[4].muted); // without restore: everything plays
    // nested: a sub-folder's header acts on its own folder, its member on the sub-folder
    auto n = folderSong ({ 0, 1, 2, 2, 1 });
    CHECK ((noteedit::groupMuteSet (n, 1) == std::vector<int> { 1, 2, 3 }) && (noteedit::groupMuteSet (n, 3) == std::vector<int> { 1, 2, 3 }));
    CHECK ((noteedit::groupMuteSet (n, 4) == std::vector<int> { 0, 1, 2, 3, 4 }));
}

TEST (folders190jf_alt_drag_drop_place)
{
    using namespace lynx;
    //                        0  1  2  3  4
    const auto s = folderSong ({ 0, 1, 1, 0, 0 });
    // track 4 dropped on the upper half of row 2 (inside the group): joins it, level 1
    CHECK ((folderdrag::dropAt (s, 4, 2, false, 0, 5) == folderdrag::Drop { 2, 1 }));
    // on the lower half of row 2 (after the group's last track): stays outside; dragged right: joins at level 1
    CHECK ((folderdrag::dropAt (s, 4, 2, true, 0, 5) == folderdrag::Drop { 3, 0 }));
    CHECK ((folderdrag::dropAt (s, 4, 2, true, folderdrag::stepPx, 5) == folderdrag::Drop { 3, 1 }));
    CHECK ((folderdrag::dropAt (s, 4, 2, true, 5 * folderdrag::stepPx, 5) == folderdrag::Drop { 3, 2 })); // (clamped: under 2)
    // a member dragged left in place: out of the group
    CHECK ((folderdrag::dropAt (s, 2, 2, false, -folderdrag::stepPx, 5) == folderdrag::Drop { 2, 0 }));
    // pressed and released in place: nothing changes
    CHECK (! folderdrag::changes (s, 2, folderdrag::dropAt (s, 2, 2, false, 0, 5)));
    // the header dropped inside its own folder: stays
    CHECK ((folderdrag::dropAt (s, 0, 1, true, 0, 5) == folderdrag::Drop { 0, 0 }));
    // over a collapsed folder's thin rows: lands after the folder
    auto c = s;
    c.playlistTracks[0].collapsed = true;
    CHECK ((folderdrag::dropAt (c, 4, 1, false, 0, 5) == folderdrag::Drop { 3, 0 }));
}

TEST (folders190jf_nested_bus)
{
    using namespace lynx;
    // 0 outer header, 1 inner header (its own insert), 2 inner member, 3 outer member; inserts 1..4
    auto s = folderSong ({ 0, 1, 2, 1 });
    s.mixer.resize (8);
    for (int t = 0; t < 4; ++t)
    {
        s.playlistTracks[(std::size_t) t].mode = PlaylistTrackMode::audio;
        s.playlistTracks[(std::size_t) t].mixerTrack = t + 1;
    }
    // the inner folder's bus: 2 sums into 1's insert (2)
    CHECK (arrange::makeGroupBus (s, 2) == 2);
    CHECK (! s.mixer[3].toMaster && s.mixer[3].sends.front().target == 2 && arrange::groupBus (s, 2) == 2);
    // the outer folder's bus: its direct members 1 and 3 (inserts 2 and 4) sum into insert 1; 2 stays on the inner bus
    CHECK (arrange::makeGroupBus (s, 0) == 1);
    CHECK (s.mixer[2].sends.front().target == 1 && s.mixer[4].sends.front().target == 1 && s.mixer[3].sends.front().target == 2);
    CHECK (arrange::groupBus (s, 3) == 1 && arrange::groupBus (s, 2) == 2);
}

TEST (folders190jf_clone_keeps_level)
{
    using namespace lynx;
    auto s = folderSong ({ 0, 1, 2, 0 });
    Clip c;
    c.track = 2;
    c.length = ppq;
    s.playlist.push_back (c);
    pltracks::CloneOptions o;
    const auto at = pltracks::cloneTracks (s, { 2 }, o);
    CHECK (at == 3 && noteedit::folderDepth (s, 3) == 2 && noteedit::folderParent (s, 3) == 1);
}

TEST (folders190jf_live_nested_groups_import)
{
    using namespace lynx;
    // round 175a's set with a group inside the group: Band ⊃ { Keys, Inner ⊃ { Vocal } }
    auto set = als175a::richSet();
    auto swap = [&set] (const std::string& from, const std::string& to) {
        const auto at = set.find (from);
        CHECK (at != std::string::npos);
        if (at != std::string::npos)
            set.replace (at, from.size(), to);
    };
    swap ("<MidiTrack Id=\"10\">" + als175a::name ("Keys") + als175a::v ("Color", "3") + als175a::v ("TrackGroupId", "-1"),
          "<MidiTrack Id=\"10\">" + als175a::name ("Keys") + als175a::v ("Color", "3") + als175a::v ("TrackGroupId", "30"));
    swap ("<AudioTrack Id=\"20\">" + als175a::name ("Vocal") + als175a::v ("Color", "20") + als175a::v ("TrackGroupId", "30"),
          "<GroupTrack Id=\"31\">" + als175a::name ("Inner") + als175a::v ("Color", "15") + als175a::v ("TrackGroupId", "30") +
              als175a::v ("TrackUnfolded", "false") + "<DeviceChain>" + als175a::mixer (310, 1.0, 0.0, true, { 0.000316 }) +
              "<AudioOutputRouting>" + als175a::v ("Target", "AudioOut/GroupTrack") +
              "</AudioOutputRouting><DeviceChain><Devices /></DeviceChain></DeviceChain></GroupTrack>" +
              "<AudioTrack Id=\"20\">" + als175a::name ("Vocal") + als175a::v ("Color", "20") + als175a::v ("TrackGroupId", "31"));
    FlpImportReport report;
    std::string error;
    const auto song = importAls (als175a::bytesOf (set), als175a::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    std::vector<std::string> names;
    std::vector<int> depths;
    for (int t = 0; t < (int) song->playlistTracks.size() && t < 4; ++t)
    {
        names.push_back (song->playlistTracks[(std::size_t) t].name);
        depths.push_back (noteedit::folderDepth (*song, t));
    }
    CHECK ((names == std::vector<std::string> { "Band", "Keys", "Inner", "Vocal" }));
    CHECK ((depths == std::vector<int> { 0, 1, 1, 2 }));
    CHECK (song->playlistTracks[2].collapsed && noteedit::isGroupHeader (*song, 2) && noteedit::isFoldedAway (*song, 3));
}
