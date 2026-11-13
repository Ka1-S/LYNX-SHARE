#pragma once

// LYNX's start-up sound, synthesised procedurally (no audio files): an airy whoosh that drops onto a
// soft "pop" when the logo lands on the splash screen, then a short plucked chime (root, fifth and
// octave) with a light stereo echo. An original sound; about 1.6 s long. No JUCE.

#include <vector>

namespace lynx::startup
{
struct StereoSound
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;
};

// Seconds from the start of the sound to the landing "pop" (the splash animation lines up with it).
constexpr double impactTime = 0.29;
constexpr double soundLength = 1.6;

// Peak level of the result (about -7 dBFS: present but not loud).
constexpr float peakLevel = 0.45f;

StereoSound makeSound (double sampleRate);
} // namespace lynx::startup
