// Round 190oo (design review 2, group "lanes"): the Clip-launch arrangement's automation lanes drawn as Live's envelope
// (CS-19, core/PlaylistViewOptions.h › cliplanes::laneClipLists), folded when a project opens (CS O-3, LYNX extra:
// cliplanes::tracksToFold) and Live's three-part scale control (O-5, LYNX extra: core/LiveScaleControl.h). Uses the
// song of tests/Playlist190jqTests.inc (Lead's volume clips on track 5, its pan clip on track 7, both lanes of track 1).

#include "core/LiveScaleControl.h"
#include "core/LynxExtras.h"
#include "core/PlaylistViewOptions.h"

TEST (lanes190oo_envelope_lane_clips_in_time_order)
{
    using namespace lynx;
    auto s = playlist190jq::songWithEnvelopes();
    // one more volume clip on track 5, at bar 1, added last: the lane's list is in time order, not in list order
    Clip extra = s.playlist[1];
    extra.start = 4 * ppq;
    s.playlist.push_back (extra);
    const auto extraIndex = (int) s.playlist.size() - 1;
    const auto p = cliplanes::project (s, 12, true);
    const auto lists = cliplanes::laneClipLists (s, p, 12);
    CHECK (lists.size() == 12);
    CHECK ((lists[5] == std::vector<int> { 1, extraIndex, 2 }));
    CHECK ((lists[7] == std::vector<int> { 3 }));
    // ordinary rows (the owner, the tempo track, the mixed track, the pattern track) draw no envelope
    for (const auto t : { 0, 1, 6, 8 })
        CHECK (lists[(std::size_t) t].empty());
    // projection off: nothing is a lane, no envelopes
    {
        const auto off = cliplanes::project (s, 12, false);
        const auto none = cliplanes::laneClipLists (s, off, 12);
        CHECK (std::all_of (none.begin(), none.end(), [] (const std::vector<int>& l) { return l.empty(); }));
    }
    // the flat parts: the first clip's start value before it, the value a clip ended on after it (vol: 0.2 -> 0.9)
    CHECK (std::abs (cliplanes::clipStartValue (s, s.playlist[1]) - 0.2f) < 1.0e-4f);
    CHECK (cliplanes::clipEndValue (s, s.playlist[1]) > 0.89f && cliplanes::clipEndValue (s, s.playlist[1]) <= 0.9f);
    // a clip that starts later in its automation (offset) starts on that point's value
    auto later = s.playlist[1];
    later.offset = 2 * ppq;
    const auto mid = cliplanes::clipStartValue (s, later);
    CHECK (mid > 0.21f && mid < 0.89f); // (half way along the ramp: 0.55 on a straight segment)
}

TEST (lanes190oo_lanes_folded_when_a_project_opens)
{
    using namespace lynx;
    CHECK (! cliplanes::foldedOnOpen); // a LYNX extra: off by default (Live-exact drawing stays on)
    auto s = playlist190jq::songWithEnvelopes();
    s.playlistTracks.resize (12);
    const auto p = cliplanes::project (s, 12, true);
    // the owner of the drawn lanes folds; tracks without lanes stay
    CHECK ((cliplanes::tracksToFold (s, p, false) == std::vector<int> { 1 }));
    // round 177b's own lanes count while they are shown
    s.playlistTracks[3].lanes.emplace_back();
    CHECK ((cliplanes::tracksToFold (s, p, true) == std::vector<int> { 1, 3 }));
    CHECK ((cliplanes::tracksToFold (s, p, false) == std::vector<int> { 1 }));
    // already folded: nothing to change (no edit is made)
    s.playlistTracks[1].lanesFolded = true;
    s.playlistTracks[3].lanesFolded = true;
    CHECK (cliplanes::tracksToFold (s, p, true).empty());
    // listed in Settings › LYNX extras under the key the main window saves
    bool listed = false;
    for (const auto& item : extras::items)
        if (item.id == "playlist.clipLanesFolded")
            listed = item.settingsKey == "ui-options.txt:clipLanesFoldedLive";
    CHECK (listed);
}

TEST (lanes190oo_live_scale_control_parts)
{
    using namespace lynx;
    CHECK (! livescale::shown); // a LYNX extra: off by default (the one key box stays)
    // the key box's 145 px: the 18 px switch, then root and mode as Live's 44 : 66, 2 px apart
    const auto p = livescale::split (145);
    CHECK (p.toggleX == 0 && p.toggleW == 18);
    CHECK (p.rootX == 20 && p.rootW == 52);
    CHECK (p.modeX == 74 && p.modeW == 71);
    CHECK (p.modeX + p.modeW == 145);
    // every x is one of the three (a gap goes to the part on its right)
    CHECK (livescale::partAt (0, 145) == livescale::Part::toggle);
    CHECK (livescale::partAt (17, 145) == livescale::Part::toggle);
    CHECK (livescale::partAt (18, 145) == livescale::Part::root);
    CHECK (livescale::partAt (71, 145) == livescale::Part::root);
    CHECK (livescale::partAt (72, 145) == livescale::Part::mode);
    CHECK (livescale::partAt (144, 145) == livescale::Part::mode);
    // a narrow box keeps all three (the switch gives way first)
    const auto n = livescale::split (40);
    CHECK (n.toggleW == 10 && n.rootW + n.modeW == 26 && n.rootW > 0 && n.modeW > 0);
    CHECK (livescale::split (0).modeW == 0 && livescale::split (-5).toggleW == 0);
    bool listed = false;
    for (const auto& item : extras::items)
        if (item.id == "controlBar.liveScale")
            listed = item.settingsKey == "ui-options.txt:liveScaleControl";
    CHECK (listed);
}
