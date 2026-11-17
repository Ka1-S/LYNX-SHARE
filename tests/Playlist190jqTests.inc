// Round 190jq: the Playlist's draggable header width (#164), pointer click (#191) and automation clips drawn as lanes
// under their track (#191.3) — core/PlaylistViewOptions.h.

#include "core/PlaylistViewOptions.h"

namespace playlist190jq
{
// Lead on instrument track 1; its volume envelope as clips on track 5 (twice) and track 7; a tempo clip on track 6; the
// Lead volume clip also on track 8 together with the tempo clip (a mixed track); a pattern clip on track 0
inline lynx::Song songWithEnvelopes()
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.channels.clear();
    s.channels.push_back (mididrop::newChannel ("Lead"));
    trackmodes::makeInstrumentTrack (s, 1, 0);
    Automation vol;
    vol.name = "Lead volume";
    vol.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    vol.points = { { 0, 0.2f, 0.0f }, { 4 * ppq, 0.9f, 0.0f } };
    Automation pan = vol;
    pan.name = "Lead pan";
    pan.target = { AutomationTargetKind::channelPan, 0, 0, 0 };
    Automation tempo;
    tempo.name = "Tempo";
    tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    tempo.points = { { 0, 0.5f, 0.0f } };
    s.automations = { vol, pan, tempo };
    const auto clip = [&s] (ClipKind kind, int source, int track, int bar) {
        Clip c;
        c.kind = kind;
        (kind == ClipKind::automation ? c.automation : c.pattern) = source;
        c.track = track;
        c.start = bar * 4 * ppq;
        c.length = 4 * ppq;
        s.playlist.push_back (c);
    };
    clip (ClipKind::pattern, 0, 0, 0);
    clip (ClipKind::automation, 0, 5, 0);
    clip (ClipKind::automation, 0, 5, 2);
    clip (ClipKind::automation, 1, 7, 0);
    clip (ClipKind::automation, 2, 6, 0);
    clip (ClipKind::automation, 0, 8, 0);
    clip (ClipKind::automation, 2, 8, 1);
    return s;
}
} // namespace playlist190jq

TEST (playlist190jq_header_width)
{
    using namespace lynx;
    const auto saved = std::pair (headerwidth::draggable, headerwidth::extra);
    headerwidth::draggable = false;
    headerwidth::extra = 80;
    // off (the default): FL's width whatever was saved, and no grip
    CHECK (headerwidth::widthOf (106) == 106);
    CHECK (! headerwidth::onGrip (106, 106));
    headerwidth::draggable = true;
    CHECK (headerwidth::widthOf (106) == 186 && headerwidth::widthOf (152) == 232); // (the same extra for every header kind)
    // the grip: the strip's last px and 3 px of the arrangement; the rest of the strip still selects tracks
    CHECK (headerwidth::onGrip (185, 186) && headerwidth::onGrip (186, 186) && headerwidth::onGrip (188, 186));
    CHECK (! headerwidth::onGrip (184, 186) && ! headerwidth::onGrip (189, 186));
    // a drag: never narrower than the layout's own width, never more than half of the Playlist
    CHECK (headerwidth::dragged (0, 50, 106, 1600) == 50);
    CHECK (headerwidth::dragged (40, -100, 106, 1600) == 0);
    CHECK (headerwidth::dragged (0, 900, 106, 1600) == headerwidth::maxExtra);
    CHECK (headerwidth::dragged (0, 900, 106, 500) == 250 - 106);
    CHECK (headerwidth::dragged (0, 50, 106, 150) == 0);
    headerwidth::extra = 9999;
    CHECK (headerwidth::widthOf (96) == 96 + headerwidth::maxExtra);
    headerwidth::draggable = saved.first;
    headerwidth::extra = saved.second;
}

TEST (playlist190jq_pointer_click)
{
    using namespace lynx;
    const auto saved = std::pair (pointerclick::buttonShown, pointerclick::on);
    using P = pointerclick::EmptyPress;
    // off: the Pencil and the Brush place the clip (FL)
    CHECK (pointerclick::onEmpty (false, EditTool::pencil, 1) == P::place);
    CHECK (pointerclick::onEmpty (false, EditTool::brush, 1) == P::place);
    // on: a single click is a pointer, a double click places; the other tools keep their own gestures
    CHECK (pointerclick::onEmpty (true, EditTool::pencil, 1) == P::pointer);
    CHECK (pointerclick::onEmpty (true, EditTool::brush, 1) == P::pointer);
    CHECK (pointerclick::onEmpty (true, EditTool::pencil, 2) == P::place);
    CHECK (pointerclick::onEmpty (true, EditTool::lasso, 1) == P::place && pointerclick::onEmpty (true, EditTool::knife, 1) == P::place);
    // the extra shows the toolbar switch lit; hiding it ends the mode; the switch turned on shows its button
    pointerclick::setButtonShown (true);
    CHECK (pointerclick::buttonShown && pointerclick::on);
    pointerclick::setOn (false);
    CHECK (pointerclick::buttonShown && ! pointerclick::on);
    pointerclick::setButtonShown (false);
    CHECK (! pointerclick::buttonShown && ! pointerclick::on);
    pointerclick::setOn (true);
    CHECK (pointerclick::buttonShown && pointerclick::on);
    pointerclick::buttonShown = saved.first;
    pointerclick::on = saved.second;
}

TEST (playlist190jq_clip_lanes_projection)
{
    using namespace lynx;
    const auto s = playlist190jq::songWithEnvelopes();
    // off: every track on its own row
    {
        const auto p = cliplanes::project (s, 12, false);
        CHECK (! p.any() && p.row (5) == 5 && p.trackAt (5) == 5);
    }
    const auto p = cliplanes::project (s, 12, true);
    CHECK (p.any());
    // tracks 5 and 7 (only Lead envelopes) are drawn under track 1, in their own order; the tempo track (no owner), the
    // mixed track 8 and the pattern track stay ordinary rows
    CHECK (p.ownerOf (5) == 1 && p.ownerOf (7) == 1 && p.childCount (1) == 2);
    CHECK (! p.isChild (6) && ! p.isChild (8) && ! p.isChild (0) && ! p.isChild (1));
    CHECK ((p.order == std::vector<int> { 0, 1, 5, 7, 2, 3, 4, 6, 8, 9, 10, 11 }));
    for (int t = 0; t < 12; ++t)
        CHECK (p.trackAt (p.row (t)) == t);
    // the song is not touched: the clips keep their tracks
    CHECK (s.playlist[1].track == 5 && s.playlist[3].track == 7);
    // a grouped track or a group header stays where it is (groups follow the order of the tracks)
    {
        auto g = s;
        g.playlistTracks.resize (12);
        g.playlistTracks[8].grouped = true; // (track 7 heads a group now)
        const auto q = cliplanes::project (g, 12, true);
        CHECK (q.ownerOf (5) == 1 && ! q.isChild (7));
    }
    // a live recording's envelope (no clip of its own control) is not drawn as a lane
    {
        auto l = s;
        l.automations[1].source.track = 3;
        const auto q = cliplanes::project (l, 12, true);
        CHECK (q.isChild (5) && ! q.isChild (7));
    }
    // the per-layout switch: on in Clip-launch (Live), off in Pattern (FL), never in Simple
    const auto saved = std::pair (cliplanes::patternOn, cliplanes::liveOn);
    cliplanes::patternOn = false;
    cliplanes::liveOn = true;
    CHECK (cliplanes::shownIn (true, false) && ! cliplanes::shownIn (false, false) && ! cliplanes::shownIn (true, true));
    cliplanes::setShownIn (false, true);
    CHECK (cliplanes::patternOn && cliplanes::shownIn (false, false) && ! cliplanes::shownIn (false, true));
    cliplanes::patternOn = saved.first;
    cliplanes::liveOn = saved.second;
}

TEST (playlist190jq_clip_lanes_moves)
{
    using namespace lynx;
    const auto s = playlist190jq::songWithEnvelopes();
    const auto p = cliplanes::project (s, 12, true);
    // rows: 0, 1, [5, 7], 2, 3, 4, 6, 8...
    CHECK (cliplanes::movedTrack (p, 0, 0, 12) == 0);
    // a lane's clip stays in its lane
    CHECK (cliplanes::movedTrack (p, 5, 1, 12) == -1 && cliplanes::movedTrack (p, 7, -2, 12) == -1);
    // an ordinary clip moves by drawn rows, never into a lane: track 0 down 1 row = track 1; 2 rows = a lane: refused;
    // 4 rows = track 2 (past the lanes)
    CHECK (cliplanes::movedTrack (p, 0, 1, 12) == 1);
    CHECK (cliplanes::movedTrack (p, 0, 2, 12) == -1);
    CHECK (cliplanes::movedTrack (p, 0, 4, 12) == 2);
    CHECK (cliplanes::movedTrack (p, 2, -1, 12) == -1 && cliplanes::movedTrack (p, 2, -3, 12) == 1);
    // past the drawn rows: the free tracks below
    CHECK (cliplanes::movedTrack (p, 8, 5, 12) == 13);
    CHECK (cliplanes::movedTrack (p, 0, -1, 12) == -1);
    // without lanes, the move is the plain track difference
    const auto off = cliplanes::project (s, 12, false);
    CHECK (cliplanes::movedTrack (off, 0, 2, 12) == 2 && cliplanes::movedTrack (off, 5, 1, 12) == 6);
}
