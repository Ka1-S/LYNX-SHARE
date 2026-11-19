#pragma once

#include <vector>
#include <cmath>

// Minimal stereo mixer, JUCE-free.
class Mixer {
public:
    Mixer();
    void setChannelGain(int channel, float gain);
    float getChannelGain(int channel) const;
    // Mix numFrames stereo frames into output (interleaved)
    void mix(const float* input, float* output, int numFrames, int channel);

private:
    std::vector<float> gains_;
};
