#pragma once

// Round 190gh (B23, community complaint #53 "no sound" / driver setup): the first-start audio check's logic, without
// JUCE (the window is ui/AudioCheck.h). The setup wizard's audio step and Help › "Test audio output..." play a test
// tone with a meter; when the user hears nothing, the check says what the meter saw and offers other devices and
// drivers to try, best first.

#include <string>
#include <utility>
#include <vector>

namespace lynx::audiocheck
{
// Above this peak (−40 dBFS) the tone reached the device: the sound goes out, just not where the user listens.
constexpr float reachedPeak = 0.01f;

enum class Verdict
{
    noDevice,        // no output device is open
    notReached,      // the tone never reached the device (its callbacks don't run, or the output is silent)
    reachedNotHeard, // it reached the device but nobody heard it: speakers elsewhere, muted, volume down
    works
};
// `heard`: the user's answer ("I heard it" / "I heard nothing"); `peak`: the highest output level the meter saw
// while the tone played (0..1).
Verdict verdict (bool deviceOpen, float peak, bool heard) noexcept;

struct Choice
{
    std::string type, name; // driver type ("Windows Audio", "ASIO", "ALSA"...) and device
    bool operator== (const Choice&) const = default;
};
// What to try after "I heard nothing", best first, at most `maxCount`, never the current device: other devices of the
// current driver type first (a headset or an interface beside the speakers), then the first device of each other
// type in the order that most often just works on that system ("windows": Windows Audio, DirectSound, then ASIO,
// then the exclusive / low-latency Windows Audio modes; "linux": PipeWire, ALSA, JACK; "macos": CoreAudio). Types
// with no device are skipped. `available`: each type with its devices, as the system lists them (default first).
std::vector<Choice> alternatives (const std::string& os, const Choice& current,
                                  const std::vector<std::pair<std::string, std::vector<std::string>>>& available, int maxCount = 4);
} // namespace lynx::audiocheck
