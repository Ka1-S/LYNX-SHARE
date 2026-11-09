#pragma once

// Interface animations (round 179a): one switch, Settings › General › Animations, for the motion that is only there to
// look smooth: panels fading in and out, the hover fade of the buttons, the wheel glide of the Playlist and the Piano
// Roll. FL Studio gates the same kind of motion with its General › Animations level (FLEngine `Fancy animations`,
// REA_rodada179a.md part D). Off: every one of them snaps at once and starts no timer. The playhead, the meters and the
// scopes are live views, not animations: they keep moving (their rate is Settings › General › Animation rate).
// Message thread only.

namespace lynx::ui::motion
{
inline bool& enabled()
{
    static bool on = true;
    return on;
}

constexpr int panelFadeMs = 110; // a docked panel opening or closing
} // namespace lynx::ui::motion
