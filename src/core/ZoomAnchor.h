#pragma once

// Issue #276 (round 190qr), a LYNX extra (off by default): zooming keeps the song position where it is on screen.
// FL Studio 2026 zooms the Playlist and the Piano roll around the mouse pointer (Ctrl+wheel; checked in round 190qr, its
// PageUp too); REAPER offers "Horizontal zoom center: edit cursor or play cursor". With this on, the wheel and the zoom
// keys of the Playlist and the Piano roll keep the song position (the playhead) in place when it is in view, so an edit
// point found by playing stays under the eye however far one zooms; off view, they zoom as before. Settings › LYNX
// extras, ui-options.txt "zoomAtSongPosition".
//
// No JUCE (tests/Issues274_190qrTests.inc).

namespace lynx::zoomanchor
{
inline bool& atSongPosition() noexcept
{
    static bool on = false;
    return on;
}

// The x (in the view's own pixels) a zoom keeps in place: the song position's `songX` when the extra is `on` and it lies
// in [left, right] (the visible grid), else `usual` (the mouse for the wheel, the view's centre for the keys).
constexpr double anchorX (bool on, double usual, double songX, double left, double right) noexcept
{
    return on && songX >= left && songX <= right ? songX : usual;
}
} // namespace lynx::zoomanchor
