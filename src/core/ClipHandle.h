#pragma once

// Issue #274 (round 190qr), a LYNX extra (off by default): the Playlist's clip gain handle stays in view.
// FL Studio 2026 draws an audio clip's gain handle (a semicircle on its bottom edge) in the middle of the clip, so when
// one zooms in on a long clip its middle is often off screen and the handle with it (REAPER keeps its item volume in the
// item's title bar). On: while the clip's middle is scrolled out of view, the handle sits in the middle of the part of
// the clip that shows; with the middle in view it is where FL has it. Settings › LYNX extras, ui-options.txt
// "gainHandleFollowsView". (A gain read-out in the clip's title bar, the issue's other option, is for the owner to
// decide: tracker/QUEUE-0.1.32.)

namespace lynx::cliphandle
{
inline bool& gainFollowsView() noexcept
{
    static bool on = false;
    return on;
}
} // namespace lynx::cliphandle
