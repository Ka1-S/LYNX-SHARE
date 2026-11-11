// --- Round 190ag (issue #147): the Playlist's Shift+arrow / Alt+arrow clip moves, as in FL -------------------------
// (FLEngine 2026 [DECOMP] FUN_00e51730 the shift, FUN_00e50fa0 its clamp and move passes, FUN_00e52360 the editor's
// KeyDown, FUN_00e7e290 the scroll that follows; FL 2026 shots in /mnt/e/lynx-work/plkeys190ag/shots/fl)

#include "core/ClipOps.h"
#include "core/KeyMap.h"

namespace
{
lynx::Clip clip190ag (int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}
} // namespace

TEST (plkeys190ag_selection_stops_together_at_the_song_start_and_keeps_its_spacing)
{
    using namespace lynx;
    std::vector<Clip> clips { clip190ag (0, 48, 96), clip190ag (1, 240, 96), clip190ag (2, 960, 96) };
    // a step that fits: every selected clip moves, the others stay
    CHECK (clipops::shiftClipsInTime (clips, { 0, 1 }, -24) == -24);
    CHECK (clips[0].start == 24 && clips[1].start == 216 && clips[2].start == 960);
    // a step that does not fit: the selection moves as far as its first clip can, the distance between the clips stays
    CHECK (clipops::shiftClipsInTime (clips, { 0, 1 }, -96) == -24);
    CHECK (clips[0].start == 0 && clips[1].start == 192 && clips[1].start - clips[0].start == 192);
    // at the song start nothing moves at all (no wrap to the end, the second clip does not creep on)
    CHECK (clipops::shiftClipsInTime (clips, { 0, 1 }, -96) == 0);
    CHECK (clips[0].start == 0 && clips[1].start == 192);
    // lengths and offsets are never touched by a shift
    CHECK (clips[0].length == 96 && clips[1].length == 96 && clips[0].offset == 0);
    // stale indices are ignored
    CHECK (clipops::shiftClipsInTime (clips, { -1, 7, 2 }, 96) == 96 && clips[2].start == 1056);
}

TEST (plkeys190ag_selection_stops_together_at_the_first_and_last_track)
{
    using namespace lynx;
    std::vector<Clip> clips { clip190ag (0, 0, 96), clip190ag (2, 0, 96), clip190ag (498, 0, 96) };
    // first track: the upper clip cannot go up, so the lower one stays too (the shape of the selection is kept)
    CHECK (clipops::shiftClipsTrack (clips, { 0, 1 }, -1) == 0 && clips[0].track == 0 && clips[1].track == 2);
    CHECK (clipops::shiftClipsTrack (clips, { 1 }, -1) == -1 && clips[1].track == 1);
    // last track (500): one step fits, the next does not; no wrap to the top
    CHECK (clipops::shiftClipsTrack (clips, { 1, 2 }, 1) == 1 && clips[1].track == 2 && clips[2].track == 499);
    CHECK (clipops::shiftClipsTrack (clips, { 1, 2 }, 1) == 0 && clips[1].track == 2 && clips[2].track == 499);
    // the time position is not touched by a track shift
    CHECK (clips[0].start == 0 && clips[1].start == 0 && clips[2].start == 0);
}

TEST (plkeys190ag_clips_moved_onto_occupied_space_overlap_like_after_a_drag)
{
    using namespace lynx;
    // FL's shift has no collision rule (FUN_00e50fa0 only adds the deltas): the clips end up on top of each other
    std::vector<Clip> clips { clip190ag (0, 0, 192), clip190ag (1, 96, 192), clip190ag (0, 384, 96) };
    CHECK (clipops::shiftClipsTrack (clips, { 1 }, -1) == -1);
    CHECK (clips.size() == 3 && clips[1].track == 0 && clips[1].start == 96 && clips[1].length == 192);
    CHECK (clips[0].track == 0 && clips[0].start == 0 && clips[0].length == 192); // the clip underneath is untouched
    CHECK (clipops::shiftClipsInTime (clips, { 1 }, 288) == 288);
    CHECK (clips[1].start == 384 && clips[2].start == 384 && clips[2].length == 96);
}

TEST (plkeys190ag_fine_step_is_one_pixel_of_ticks_and_never_less_than_a_tick)
{
    using namespace lynx;
    CHECK (clipops::pixelStepTicks (0.125) == 8);  // zoomed out: 8 ticks per pixel
    CHECK (clipops::pixelStepTicks (0.4) == 3);    // 2.5 ticks per pixel, rounded
    CHECK (clipops::pixelStepTicks (1.0) == 1);
    CHECK (clipops::pixelStepTicks (12.0) == 1);   // zoomed in: still one whole tick
    CHECK (clipops::pixelStepTicks (0.0) == 1);
}

TEST (plkeys190ag_view_follows_the_leading_edge_of_the_moved_clips)
{
    using namespace lynx;
    // a 640 px view at 0.5 px per tick shows 1280 ticks; FL's margin is 1/32 of the view = 20 px = 40 ticks
    const auto view = 640.0, zoom = 0.5;
    // well inside the view: no scroll, in either direction
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 1600, true) == 1000.0);
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 1600, false) == 1000.0);
    // moving later past the right margin (1000 + 1240): the edge lands three margins (120 ticks) inside the right side
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 2241, true) == 2241.0 - 1160.0);
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 2240, true) == 1000.0);
    // moving earlier past the left margin (1000 + 40): three margins inside the left side, never before the song start
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 1039, false) == 1039.0 - 120.0);
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 1040, false) == 1000.0);
    CHECK (clipops::scrollToShowEdge (100.0, view, zoom, 60, false) == 0.0);
    // the side the move does not go towards never scrolls (a long clip's far end may hang out of the view)
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 500, true) == 1000.0);
    CHECK (clipops::scrollToShowEdge (1000.0, view, zoom, 5000, false) == 1000.0);
    CHECK (clipops::scrollToShowEdge (1000.0, 0.0, zoom, 5000, true) == 1000.0);
}

TEST (plkeys190ag_arrow_keys_are_fl_s_and_stay_inside_the_playlist)
{
    using namespace lynx;
    keys::KeyMap map;
    const auto both = (std::uint8_t) (keys::alt | keys::shift);
    CHECK (map.find (keys::Chord { keys::left, keys::shift }, keys::Context::playlist, false) == "playlist.shiftLeft");
    CHECK (map.find (keys::Chord { keys::right, keys::shift }, keys::Context::playlist, false) == "playlist.shiftRight");
    CHECK (map.find (keys::Chord { keys::up, keys::shift }, keys::Context::playlist, false) == "playlist.shiftUp");
    CHECK (map.find (keys::Chord { keys::down, keys::shift }, keys::Context::playlist, false) == "playlist.shiftDown");
    CHECK (map.find (keys::Chord { keys::left, keys::alt }, keys::Context::playlist, false) == "playlist.nudgeLeft");
    CHECK (map.find (keys::Chord { keys::right, keys::alt }, keys::Context::playlist, false) == "playlist.nudgeRight");
    CHECK (map.find (keys::Chord { keys::left, both }, keys::Context::playlist, false) == "playlist.nudgeLeft");
    CHECK (map.find (keys::Chord { keys::right, both }, keys::Context::playlist, false) == "playlist.nudgeRight");
    // the neighbours keep their own keys: time selection (Ctrl), the Mixer's insert move (Alt), plain arrows unbound
    CHECK (map.find (keys::Chord { keys::left, keys::ctrl }, keys::Context::playlist, false) == "playlist.timePrevious");
    CHECK (map.find (keys::Chord { keys::left, keys::alt }, keys::Context::mixer, false) == "mixer.moveLeft");
    CHECK (map.find (keys::Chord { keys::right, keys::alt }, keys::Context::mixer, false) == "mixer.moveRight");
    CHECK (map.find (keys::Chord { keys::left, keys::none }, keys::Context::playlist, false).empty());
    CHECK (map.find (keys::Chord { keys::up, keys::alt }, keys::Context::playlist, false) != "playlist.nudgeLeft");
    // outside the Playlist the clip moves are not reachable
    CHECK (map.find (keys::Chord { keys::left, both }, keys::Context::channelRack, false) != "playlist.nudgeLeft");
    CHECK (map.find (keys::Chord { keys::left, keys::shift }, keys::Context::mixer, false) != "playlist.shiftLeft");
}
