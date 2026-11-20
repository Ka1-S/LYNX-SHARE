#pragma once

// Follow's pause (round 190im, R15; Live's manual 6.2 / 8.7: "Follow will pause if you make an edit or scroll the view
// horizontally", it goes on with the next play / stop or a Follow click), kept apart from the UI so it can be tested.
//
// Round 190kp (QA of 0.1.30): a pause only counts where pausing is on (the Clip-launch layout, or the option set by
// hand). Before, a pause made in the Clip-launch layout stayed after a switch to the Pattern or Simple layout during
// playback, and the Playlist stopped following until the next stop, although those layouts never pause. A layout
// switch is not a scroll by hand either: it lifts every pause, and the views' scroll changes that follow it within
// `layoutGraceMs` (the new layout's sizes) pause nothing.

#include <array>
#include <cstddef>

namespace lynx::followpause
{
enum View : int
{
    playlist,
    pianoRoll,
    channelRack,
    audioEditor,
    viewCount
};

struct State
{
    std::array<bool, viewCount> paused {};
    bool lastPlaying = false;
    double layoutChangedMs = -1.0e12;
    static constexpr double layoutGraceMs = 750.0;

    // an edit by hand (or a scroll seen outside a layout switch): `pausing` = the layout or the option pauses Follow,
    // `following` = Follow is on for the view
    void pause (int view, bool pausing, bool following) noexcept
    {
        if (pausing && following && view >= 0 && view < viewCount)
            paused[(std::size_t) view] = true;
    }
    // a sideways scroll a view saw between two frames, `nowMs` on any steady millisecond clock
    void pauseForScroll (int view, bool pausing, bool following, double nowMs) noexcept
    {
        if (nowMs - layoutChangedMs >= layoutGraceMs)
            pause (view, pausing, following);
    }
    bool isPaused (int view, bool pausing) const noexcept
    {
        return pausing && view >= 0 && view < viewCount && paused[(std::size_t) view];
    }
    bool follows (int view, bool following, bool pausing) const noexcept { return following && ! isPaused (view, pausing); }
    void resume (int view) noexcept
    {
        if (view >= 0 && view < viewCount)
            paused[(std::size_t) view] = false;
    }
    // each view's frame step tells the transport's state: a play or a stop lifts every pause
    void noteTransport (bool playing) noexcept
    {
        if (playing != lastPlaying)
            paused = {};
        lastPlaying = playing;
    }
    void layoutChanged (double nowMs) noexcept
    {
        paused = {};
        layoutChangedMs = nowMs;
    }
};
} // namespace lynx::followpause
