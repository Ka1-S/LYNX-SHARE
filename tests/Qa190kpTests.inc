// --- round qa190kp (QA of 0.1.30): Follow's pause (core/FollowPause.h) after a layout switch during playback.

#include "core/FollowPause.h"

TEST (qa190kp_pause_counts_only_where_pausing_is_on)
{
    using namespace lynx::followpause;
    State s;
    s.noteTransport (true);
    // Clip-launch layout (pausing): a scroll by hand pauses the Playlist's Follow
    s.pauseForScroll (playlist, true, true, 10000.0);
    CHECK (s.isPaused (playlist, true));
    CHECK (! s.follows (playlist, true, true));
    // the same pause, read in the Pattern / Simple layout (not pausing): Follow goes on
    CHECK (! s.isPaused (playlist, false));
    CHECK (s.follows (playlist, true, false));
    // nothing pauses where pausing is off, nor a view whose Follow is off
    State t;
    t.pause (pianoRoll, false, true);
    t.pause (pianoRoll, true, false);
    CHECK (! t.paused[pianoRoll]);
}

TEST (qa190kp_layout_switch_lifts_pauses_and_its_scrolls_pause_nothing)
{
    using namespace lynx::followpause;
    State s;
    s.noteTransport (true);
    s.pause (playlist, true, true);
    s.pause (pianoRoll, true, true);
    s.layoutChanged (5000.0);
    CHECK (! s.paused[playlist] && ! s.paused[pianoRoll]);
    // the new layout's sizes move the views right after the switch: no pause
    s.pauseForScroll (playlist, true, true, 5000.0 + State::layoutGraceMs - 1.0);
    CHECK (! s.isPaused (playlist, true));
    // a scroll by hand later still pauses (Live's rule in the Clip-launch layout)
    s.pauseForScroll (playlist, true, true, 5000.0 + State::layoutGraceMs + 1.0);
    CHECK (s.isPaused (playlist, true));
}

TEST (qa190kp_play_or_stop_lifts_pauses_and_a_click_resumes)
{
    using namespace lynx::followpause;
    State s;
    s.noteTransport (true);
    s.pause (audioEditor, true, true);
    s.noteTransport (true); // still playing: kept
    CHECK (s.isPaused (audioEditor, true));
    s.noteTransport (false); // a stop lifts it
    CHECK (! s.isPaused (audioEditor, true));
    s.pause (channelRack, true, true);
    s.resume (channelRack);
    CHECK (! s.isPaused (channelRack, true));
    s.resume (-1); // (out of range: ignored)
    s.pause (99, true, true);
    CHECK (! s.isPaused (99, true));
}
