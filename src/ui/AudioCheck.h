#pragma once

// Round 190gh (B23, community complaint #53 "no sound" / driver setup): the audio check of the setup wizard's audio
// step and of Help › "Test audio output...". "Play test sound" plays a one-second 440 Hz tone through the open output
// device while a meter shows what reaches it; then "I heard it" / "I heard nothing". After "nothing" the panel says
// what the meter saw (the tone never reached the device, or it did and went somewhere the user doesn't listen) and
// offers up to four other devices and drivers (core/AudioCheck.h), each a click that switches to it and plays the
// tone again. LYNX's own (FL has no such check).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// The panel. Its height follows its state: audioCheckHeight() gives it for a width, and `onHeightChanged` runs when it
// changes (the parent lays out again). `persistent` false (--snapshot): nothing is played or switched.
std::unique_ptr<juce::Component> makeAudioCheckPanel (AudioEngine& engine, bool persistent, std::function<void()> onHeightChanged = {});
int audioCheckHeight (const juce::Component& panel, int width);
// Help › Test audio output...: the panel and the device's name in a window of its own.
void showAudioCheck (AudioEngine& engine);
// --snapshot: the panel in its states (0 ready, 1 playing, 2 heard, 3 reached the device but not heard, 4 never
// reached it), with sample devices.
std::unique_ptr<juce::Component> makeAudioCheckForSnapshot (AudioEngine& engine, int state);
} // namespace lynx
