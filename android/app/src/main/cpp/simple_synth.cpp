#include "simple_synth.h"

#include <cmath>
#include <algorithm>

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

// ---------- SimpleSynth ----------

SimpleSynth::SimpleSynth() {
    voices_.resize(12);
}

void SimpleSynth::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
}

double SimpleSynth::midiToFrequency(int note) const {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

void SimpleSynth::setWaveform(int wf) {
    waveform_ = wf;
}

int SimpleSynth::getWaveform() const {
    return waveform_;
}

double SimpleSynth::waveformSample(int wf, double phase) const {
    switch (wf) {
        case 0: return std::sin(phase); // Sine
        case 1: return (phase < kTwoPi * 0.5) ? 1.0 : -1.0; // Square
        case 2: return 2.0 * (phase / kTwoPi - 0.5); // Saw
        case 3: { // Triangle
            double t = phase / kTwoPi;
            return 2.0 * std::abs(2.0 * (t - std::floor(t + 0.5)) - 0.5) - 1.0;
        }
    }
    return 0.0;
}

void SimpleSynth::noteOn(int note, int velocity) {
    double amp = (velocity / 127.0f) * 0.3f;
    for (auto& v : voices_) {
        if (!v.active) {
            v.active = true;
            v.note = note;
            v.phase = 0.0;
            v.phaseIncrement = (kTwoPi * midiToFrequency(note)) / sampleRate_;
            v.amplitude = amp;
            return;
        }
    }
    // Voice steal
    if (!voices_.empty()) {
        auto& v = voices_[0];
        v.active = true;
        v.note = note;
        v.phase = 0.0;
        v.phaseIncrement = (kTwoPi * midiToFrequency(note)) / sampleRate_;
        v.amplitude = amp;
    }
}

void SimpleSynth::noteOff(int note) {
    for (auto& v : voices_) {
        if (v.active && v.note == note) {
            v.active = false;
            break;
        }
    }
}

void SimpleSynth::render(float* buffer, int numFrames) {
    for (int i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            sample += static_cast<float>(waveformSample(waveform_, v.phase) * v.amplitude);
            v.phase += v.phaseIncrement;
            while (v.phase >= kTwoPi) v.phase -= kTwoPi;
            while (v.phase < 0.0) v.phase += kTwoPi;
        }
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i * 2] = sample;
        buffer[i * 2 + 1] = sample;
    }
}

// ---------- DrumSynth ----------

DrumSynth::DrumSynth() {
    voices_.resize(8);
}

void DrumSynth::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
}

void DrumSynth::trigger(DrumType type, float velocity) {
    double amp = velocity * 0.6f;
    double duration = 0.0;
    switch (type) {
        case DrumType::Kick:   duration = 0.3; break;
        case DrumType::Snare:  duration = 0.15; break;
        case DrumType::Hihat:  duration = 0.08; break;
    }
    for (auto& v : voices_) {
        if (!v.active) {
            v.active = true;
            v.type = type;
            v.phase = 0.0;
            v.time = 0.0;
            v.duration = duration;
            v.amplitude = amp;
            v.noisePhase = 0.0;
            return;
        }
    }
}

void DrumSynth::render(float* buffer, int numFrames) {
    double dt = 1.0 / sampleRate_;
    for (int i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            v.time += dt;
            if (v.time > v.duration) {
                v.active = false;
                continue;
            }
            double env = 1.0 - v.time / v.duration; // linear decay
            double osc = 0.0;
            switch (v.type) {
                case DrumType::Kick: {
                    double freq = 100.0 * std::exp(-v.time * 8.0); // pitch drop
                    osc = std::sin(v.phase) * env * 0.8;
                    v.phase += kTwoPi * freq * dt;
                    break;
                }
                case DrumType::Snare: {
                    osc = (std::sin(v.phase) * 0.4 + (v.noisePhase - 0.5) * 0.6) * env * 0.6;
                    v.phase += kTwoPi * 180.0 * dt;
                    v.noisePhase += 0.1;
                    break;
                }
                case DrumType::Hihat: {
                    osc = (v.noisePhase - 0.5) * env * 0.4;
                    v.noisePhase += 0.3;
                    break;
                }
            }
            sample += static_cast<float>(osc * v.amplitude);
        }
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        buffer[i * 2] = sample;
        buffer[i * 2 + 1] = sample;
    }
}

// ---------- DelayEffect ----------

DelayEffect::DelayEffect() {}

void DelayEffect::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
    bufferL_.assign(static_cast<std::size_t>(sampleRate * 2.0), 0.0f); // 2s max
    bufferR_.assign(bufferL_.size(), 0.0f);
}

void DelayEffect::setDelayMs(float delayMs) {
    delaySamples_ = static_cast<int>(sampleRate_ * delayMs / 1000.0);
    if (delaySamples_ < 1) delaySamples_ = 1;
    if (delaySamples_ > static_cast<int>(bufferL_.size())) delaySamples_ = static_cast<int>(bufferL_.size());
}

void DelayEffect::setFeedback(float feedback) {
    feedback_ = feedback;
}

void DelayEffect::setMix(float mix) {
    mix_ = mix;
}

void DelayEffect::process(float* buffer, int numFrames) {
    for (int i = 0; i < numFrames * 2; i += 2) {
        float inL = buffer[i];
        float inR = buffer[i + 1];

        int readPos = writePos_ - delaySamples_;
        if (readPos < 0) readPos += static_cast<int>(bufferL_.size());

        float dL = bufferL_[readPos];
        float dR = bufferR_[readPos];

        bufferL_[writePos_] = inL + dL * feedback_;
        bufferR_[writePos_] = inR + dR * feedback_;

        writePos_ = (writePos_ + 1) % static_cast<int>(bufferL_.size());

        buffer[i] = inL + dL * mix_;
        buffer[i + 1] = inR + dR * mix_;
    }
}

// ---------- ReverbEffect ----------

ReverbEffect::ReverbEffect() {
    // Comb delay lengths (in samples at 44.1kHz, prime numbers for decorrelation)
    combDelay_[0] = 1116;
    combDelay_[1] = 1188;
    combDelay_[2] = 1277;
    combDelay_[3] = 1356;
    for (int i = 0; i < 4; ++i) {
        combBuf_[i].assign(combDelay_[i], 0.0f);
    }
}

void ReverbEffect::setSampleRate(double sampleRate) {
    sampleRate_ = sampleRate;
}

void ReverbEffect::setRoomSize(float roomSize) {
    roomSize_ = roomSize;
    // Scale comb feedback by room size
    combFeedback_[0] = 0.75f * roomSize_;
    combFeedback_[1] = 0.78f * roomSize_;
    combFeedback_[2] = 0.81f * roomSize_;
    combFeedback_[3] = 0.84f * roomSize_;
}

void ReverbEffect::setDamping(float damping) {
    damping_ = damping;
}

void ReverbEffect::setMix(float mix) {
    mix_ = mix;
}

float ReverbEffect::processComb(int idx, float input) {
    float& buf = combBuf_[idx];
    int& pos = combPos_[idx];
    float delay = static_cast<float>(combDelay_[idx]);
    int readPos = pos - static_cast<int>(delay);
    if (readPos < 0) readPos += static_cast<int>(buf.size());
    float out = buf[readPos];
    // Low-pass damping
    float damp = combDampingHist_[idx] * damping_;
    float newSample = input + out * combFeedback_[idx] * (1.0f - damping_) + damp;
    buf[pos] = newSample;
    combDampingHist_[idx] = newSample;
    pos = (pos + 1) % static_cast<int>(buf.size());
    return out;
}

void ReverbEffect::process(float* buffer, int numFrames) {
    for (int i = 0; i < numFrames * 2; i += 2) {
        float inL = buffer[i];
        float inR = buffer[i + 1];

        float wetL = 0.0f;
        float wetR = 0.0f;
        for (int c = 0; c < 4; ++c) {
            float outL = processComb(c, inL);
            float outR = processComb(c, inR);
            wetL += outL * 0.25f;
            wetR += outR * 0.25f;
        }

        buffer[i] = inL * (1.0f - mix_) + wetL * mix_;
        buffer[i + 1] = inR * (1.0f - mix_) + wetR * mix_;
    }
}
