#pragma once

// Issue #280 (round 190qr): what LYNX does when an export (Export audio, stems, sections) has finished — two LYNX extras,
// off by default. FL Studio 2026 closes its render window and says nothing (its only option is "Show file(s) when
// complete"; FLEngine has no render sound or completion box, round 190qr strings check; LYNX puts the result in the
// hint bar, round 170h). Settings › LYNX extras, ui-options.txt "exportDoneSound" / "exportDoneWindow".
//
// No JUCE.

#include <functional>

namespace lynx::exportdone
{
// "Play a sound when an export finishes": LYNX's short chime (startup::makeDoneSound) through the audio device
inline bool& sound() noexcept
{
    static bool on = false;
    return on;
}
// "Show a window when an export finishes": the file(s), length, peak and loudness, with Show in folder and OK
inline bool& window() noexcept
{
    static bool on = false;
    return on;
}
// The app plays the chime (bound by Main.cpp to its engine's preview voice); empty in tests and off-screen runs
inline std::function<void()> playSound;
} // namespace lynx::exportdone
