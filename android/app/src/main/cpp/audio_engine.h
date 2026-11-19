#pragma once

#include "project_file.h"
#include "importer_stub.h"
#include "simple_synth.h"
#include "song_model.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cmath>

// LYNX audio engine — JUCE-free, C++20.
// All audio buffers are std::vector<float>, interleaved stereo.

enum class Waveform : int8_t {
    Sine = 0,
    Square = 1,
    Saw = 2,
    Triangle = 3,
};

struct AdsrEnvelope {
    double attackSec = 0.05;
    double decaySec = 0.1;
    double sustainLevel = 0.7;
    double releaseSec = 0.2;

    enum class State : int8_t { Off, Attack, Decay, Sustain, Release, Done };
    State state = State::Off;
    double time = 0.0;
    double level = 0.0;
    bool noteOn = false;

    void noteOnEnv();
    void noteOffEnv();
    double process(double dt);
    bool isDone() const { return state == State::Done; }
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    void setAssetPath(const std::string& path);
    bool initialize();
    void start();
    void stop();
    void togglePlayback();
    bool isPlaying() const;
    double getTempoBpm() const;
    void setTempoBpm(double bpm);

    // Render interleaved stereo buffer
    void render(float* buffer, int numFrames);

    // Note events
    void noteOn(int note, int velocity);
    void noteOff(int note);

    // Waveform selection (global for now)
    void setWaveform(Waveform wf);
    Waveform getWaveform() const;

    // Set sample rate (called by output)
    void setSampleRate(double sampleRate);
    double getSampleRate() const;

    // Project save/load (native .lynx format)
    bool saveProject(const std::string& path);
    bool loadProject(const std::string& path);
    Song getSong() const;
    void applySong(const Song& song);

    // Drum trigger
    void triggerDrum(DrumType type, float velocity);

    // Effects parameters
    void setDelayMix(float mix);
    void setDelayFeedback(float feedback);
    void setReverbMix(float mix);
    void setReverbRoomSize(float roomSize);
    float getDelayMix() const;
    float getDelayFeedback() const;
    float getReverbMix() const;
    float getReverbRoomSize() const;

private:
    std::string assetPath_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> playing_{false};
    std::atomic<double> tempoBpm_{120.0};
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<Waveform> waveform_{Waveform::Sine};

    // Project data (Song model, Fase 3 PORTING.md)
    Song song_;

    // Drum synth (independent from SimpleSynth voices)
    DrumSynth drumSynth_;

    // Effects
    DelayEffect delayEffect_;
    ReverbEffect reverbEffect_;

    // Effect parameters
    float delayMix_ = 0.3f;
    float delayFeedback_ = 0.3f;
    float reverbMix_ = 0.3f;
    float reverbRoomSize_ = 0.5f;

    struct Voice {
        bool active = false;
        int note = 0;
        double phase = 0.0;
        double phaseIncrement = 0.0;
        float amplitude = 0.0f;
        AdsrEnvelope env;
    };
    std::vector<Voice> voices_;

    std::mutex mutex_;

    double noteToFrequency(int note) const;
    double waveformSample(Waveform wf, double phase) const;
    void updatePhaseIncrement();
};
