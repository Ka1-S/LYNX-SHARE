#pragma once

// Issue #217 (round 190ng): Esc closes the focused window. FL 2026 does it (checked 2026-11-19 in FL 26.1.7 with "Ookay -
// Thief": the focused Channel rack, Mixer, Piano roll and Playlist all closed on Esc; round 170h: plugin, channel and
// tool windows). LYNX keeps one thing of its own before that: while the Playlist or the Piano roll has a selection, the
// first Esc clears it (as it did, and as issue #218 asked); the next one closes the window.
//
// One switch for all of it (Settings › LYNX extras, ui-options.txt "escClosesWindows"), on by default as in FL; off,
// Esc leaves every window open (dialogs still close on it). The panels close only in the Pattern layout, where they are
// FL's windows; the Clip-launch and Simple layouts keep their views. No JUCE: tests/Issues190ngTests.inc.

namespace lynx::escclose
{
inline bool& enabled() noexcept
{
    static bool on = true;
    return on;
}

// Esc on a docked or floating panel (Playlist, Channel rack, Piano roll, Mixer) that nothing else used.
inline bool closesPanel (bool on, bool patternLayout, bool panelShown) noexcept
{
    return on && patternLayout && panelShown;
}
} // namespace lynx::escclose
