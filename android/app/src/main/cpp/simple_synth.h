#pragma once

#include <cstdint>
#include <vector>
#include <string>

// ---------- Simple polyphonic synth (sine/square/saw/triangle) ----------

struct SimpleSynthVoice {
    bool active = false;
    int note = 0;
    double phase = 0.0;
    double phaseIncrement = 0.0;
    float amplitude = 0.0f;
};

class SimpleSynth {
public:
    SimpleSynth();
    void setSampleRate(double sampleRate);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    // Render numFrames stereo frames into interleaved buffer
    void render(float* buffer, int numFrames);

    void setWaveform(int wf);
    int getWaveform() const;

private:
    std::vector<SimpleSynthVoice> voices_;
    double sampleRate_ = 44100.0;
    int waveform_ = 0; // 0=Sine,1=Square,2=Saw,3=Triangle

    double midiToFrequency(int note) const;
    double waveformSample(int wf, double phase) const;
};

// ---------- Drum synth (kick, snare, hihat) ----------

enum class DrumType : int8_t { Kick = 0, Snare = 1, Hihat = 2 };

struct DrumVoice {
    bool active = false;
    DrumType type = DrumType::Kick;
    double phase = 0.0;
    double time = 0.0; // seconds since note on
    double duration = 0.0; // seconds
    float amplitude = 0.0f;
    double noisePhase = 0.0;
};

class DrumSynth {
public:
    DrumSynth();
    void setSampleRate(double sampleRate);
    void trigger(DrumType type, float velocity);
    void render(float* buffer, int numFrames);

private:
    std::vector<DrumVoice> voices_;
    double sampleRate_ = 44100.0;
};

// ---------- Effects ----------

class DelayEffect {
public:
    DelayEffect();
    void setSampleRate(double sampleRate);
    void setDelayMs(float delayMs);
    void setFeedback(float feedback);
    void setMix(float mix);
    // Process interleaved stereo in-place
    void process(float* buffer, int numFrames);

private:
    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int delaySamples_ = 0;
    float feedback_ = 0.3f;
    float mix_ = 0.3f;
    double sampleRate_ = 44100.0;
};

class ReverbEffect {
public:
    ReverbEffect();
    void setSampleRate(double sampleRate);
    void setRoomSize(float roomSize);
    void setDamping(float damping);
    void setMix(float mix);
    void process(float* buffer, int numFrames);

private:
    // Simple feedback delay network (4 parallel comb filters)
    std::vector<float> combBuf_[4];
    int combPos_[4] = {0, 0, 0, 0};
    int combDelay_[4];
    float combFeedback_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float combDamping_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float combDampingHist_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float roomSize_ = 0.5f;
    float damping_ = 0.5f;
    float mix_ = 0.3f;
    double sampleRate_ = 44100.0;

    float processComb(int idx, float input);
};
