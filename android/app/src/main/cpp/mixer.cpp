#include "mixer.h"

Mixer::Mixer() {
    gains_.assign(16, 0.8f); // 16 channels, default gain
}

void Mixer::setChannelGain(int channel, float gain) {
    if (channel >= 0 && channel < static_cast<int>(gains_.size())) {
        gains_[channel] = gain;
    }
}

float Mixer::getChannelGain(int channel) const {
    if (channel >= 0 && channel < static_cast<int>(gains_.size())) {
        return gains_[channel];
    }
    return 0.0f;
}

void Mixer::mix(const float* input, float* output, int numFrames, int channel) {
    float gain = getChannelGain(channel);
    for (int i = 0; i < numFrames * 2; ++i) {
        output[i] += input[i] * gain;
    }
}
