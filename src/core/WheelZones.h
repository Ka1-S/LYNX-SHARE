#pragma once

// Issue #186 (round 190iu), a LYNX extra: "Wheel: scroll over the track headers, zoom over the arrangement". With it on,
// the plain mouse wheel scrolls the tracks while the pointer is over the track header column and zooms the time line
// while it is over the arrangement (the clips, the ruler); Ctrl+wheel does the other one in each place. Shift and Alt
// keep their meaning (Shift+wheel over a header moves the track, over a clip nudges; Alt+wheel sets the track heights).
// REAPER's "Track control panel" wheel context is the model the issue gives. FL Studio and Ableton Live both scroll with
// the plain wheel everywhere and zoom with Ctrl+wheel (FL: Playlist; Live: Arrangement), so it is off by default.
//
// Used by the Playlist, which is also the Clip-launch layout's arrangement. An app setting (ui-options.txt
// "wheelZones"), not saved in projects. No JUCE: tested in tests/Workflow190iuTests.inc.

#include <atomic>

namespace lynx::wheelzones
{
inline std::atomic<bool>& enabledFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool enabled() noexcept { return enabledFlag().load (std::memory_order_relaxed); }
inline void setEnabled (bool on) noexcept { enabledFlag().store (on, std::memory_order_relaxed); }

// Whether a vertical wheel step zooms the time line (true) or scrolls (false). `ctrl` is the platform's zoom modifier
// (Ctrl, Cmd on macOS); Shift / Alt wheels never get here. A purely sideways wheel or swipe (`vertical` false) always
// scrolls sideways while the switch is on (off: exactly as before).
inline bool zooms (bool on, bool overHeaders, bool ctrl, bool vertical = true) noexcept
{
    if (! on)
        return ctrl; // FL and Live: Ctrl+wheel zooms, the plain wheel scrolls, wherever the pointer is
    if (! vertical)
        return false;
    return overHeaders ? ctrl : ! ctrl;
}
// the same with the app setting
inline bool zoomsHere (bool overHeaders, bool ctrl, bool vertical = true) noexcept { return zooms (enabled(), overHeaders, ctrl, vertical); }
} // namespace lynx::wheelzones
