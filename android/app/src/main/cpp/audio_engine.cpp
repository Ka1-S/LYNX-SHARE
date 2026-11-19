#include "audio_engine.h"

#include <algorithm>
#include <vector>

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

double midiNoteToFrequency(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

} // namespace

// ---------- ADSR Envelope ----------

void AdsrEnvelope::noteOnEnv() {
    state = State::Attack;
    time = 0.0;
    level = 0.0;
    noteOn = true;
}

void AdsrEnvelope::noteOffEnv() {
    if (state == State::Off) return;
    state = State::Release;
    time = 0.0;
    noteOn = false;
}

double AdsrEnvelope::process(double dt) {
    switch (state) {
        case State::Off:
            level = 0.0;
            break;
        case State::Attack: {
            time += dt;
            if (attackSec > 0.0 && time < attackSec) {
                level = time / attackSec;
            } else {
                level = 1.0;
                state = State::Decay;
                time = 0.0;
            }
            break;
        }
        case State::Decay: {
            time += dt;
            if (decaySec > 0.0 && time < decaySec) {
                level = 1.0 - (1.0 - sustainLevel) * (time / decaySec);
            } else {
                level = sustainLevel;
                state = State::Sustain;
            }
            break;
        }
        case State::Sustain:
            level = sustainLevel;
            break;
        case State::Release: {
            time += dt;
            if (releaseSec > 0.0 && time < releaseSec) {
                level = sustainLevel * (1.0 - time / releaseSec);
            } else {
                level = 0.0;
                state = State::Done;
            }
            break;
        }
        case State::Done:
            level = 0.0;
            break;
    }
    return level;
}

// ---------- Audio Engine ----------

AudioEngine::AudioEngine() {
    voices_.resize(12);
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::setAssetPath(const std::string& path) {
    assetPath_ = path;
}

bool AudioEngine::initialize() {
    if (initialized_.load()) return true;
    phaseIncrement_ = 0.0; // placeholder
    initialized_.store(true);
    return true;
}

void AudioEngine::start() { playing_.store(true); }
void AudioEngine::stop() { playing_.store(false); }
void AudioEngine::togglePlayback() { playing_.store(!playing_.load()); }
bool AudioEngine::isPlaying() const { return playing_.load(); }

double AudioEngine::getTempoBpm() const { return tempoBpm_.load(); }
void AudioEngine::setTempoBpm(double bpm) {
    if (bpm > 0.0 && bpm < 999.0) tempoBpm_.store(bpm);
}
void AudioEngine::setWaveform(Waveform wf) { waveform_.store(wf); }
Waveform AudioEngine::getWaveform() const { return waveform_.load(); }
void AudioEngine::setSampleRate(double sr) { sampleRate_.store(sr); }
double AudioEngine::getSampleRate() const { return sampleRate_.load(); }

bool AudioEngine::saveProject(const std::string& path) {
    return ::saveProject(path, song_);
}

bool AudioEngine::loadProject(const std::string& path) {
    auto song = ::loadProject(path);
    if (!song) return false;
    song_ = *song;
    setTempoBpm(song_.bpm);
    return true;
}

Song AudioEngine::getSong() const {
    return song_;
}

void AudioEngine::applySong(const Song& song) {
    song_ = song;
    setTempoBpm(song.bpm);
}

void AudioEngine::triggerDrum(DrumType type, float velocity) {
    drumSynth_.trigger(type, velocity);
}

void AudioEngine::setDelayMix(float mix) { delayMix_ = mix; delayEffect_.setMix(mix); }
void AudioEngine::setDelayFeedback(float feedback) { delayFeedback_ = feedback; delayEffect_.setFeedback(feedback); }
void AudioEngine::setReverbMix(float mix) { reverbMix_ = mix; reverbEffect_.setMix(mix); }
void AudioEngine::setReverbRoomSize(float roomSize) { reverbRoomSize_ = roomSize; reverbEffect_.setRoomSize(roomSize); }
float AudioEngine::getDelayMix() const { return delayMix_; }
float AudioEngine::getDelayFeedback() const { return delayFeedback_; }
float AudioEngine::getReverbMix() const { return reverbMix_; }
float AudioEngine::getReverbRoomSize() const { return reverbRoomSize_; }

double AudioEngine::noteToFrequency(int note) const {
    return midiNoteToFrequency(note);
}

double AudioEngine::waveformSample(Waveform wf, double phase) const {
    switch (wf) {
        case Waveform::Sine:
            return std::sin(phase);
        case Waveform::Square:
            return (phase < kTwoPi * 0.5) ? 1.0 : -1.0;
        case Waveform::Saw:
            return 2.0 * (phase / kTwoPi - 0.5);
        case Waveform::Triangle: {
            double t = phase / kTwoPi;
            return 2.0 * std::abs(2.0 * (t - std::floor(t + 0.5)) - 0.5) - 1.0;
        }
    }
    return 0.0;
}

void AudioEngine::updatePhaseIncrement() {
    // Not used per-voice anymore; voices compute their own increment.
}

void AudioEngine::noteOn(int note, int velocity) {
    std::lock_guard<std::mutex> lock(mutex_);
    double amp = (velocity / 127.0f) * 0.4f;
    // Find free voice
    for (auto& v : voices_) {
        if (!v.active) {
            v.active = true;
            v.note = note;
            v.phase = 0.0;
            v.phaseIncrement = (kTwoPi * noteToFrequency(note)) / sampleRate_.load();
            v.amplitude = amp;
            v.env.noteOnEnv();
            return;
        }
    }
    // Steal oldest active voice
    for (auto& v : voices_) {
        if (v.active) {
            v.active = true;
            v.note = note;
            v.phase = 0.0;
            v.phaseIncrement = (kTwoPi * noteToFrequency(note)) / sampleRate_.load();
            v.amplitude = amp;
            v.env.noteOnEnv();
            return;
        }
    }
}

void AudioEngine::noteOff(int note) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) {
        if (v.active && v.note == note) {
            v.env.noteOffEnv();
        }
    }
}

void AudioEngine::render(float* buffer, int numFrames) {
    double dt = 1.0 / sampleRate_.load();
    Waveform wf = waveform_.load();

    if (!playing_.load()) {
        std::fill(buffer, buffer + numFrames * 2, 0.0f);
        // Release all voices
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& v : voices_) {
            if (v.active) v.env.noteOffEnv();
        }
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<float> dry(numFrames * 2, 0.0f);
    for (int i = 0; i < numFrames; ++i) {
        float sample = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            double envLevel = v.env.process(dt);
            if (v.env.isDone()) {
                v.active = false;
                continue;
            }
            double osc = waveformSample(wf, v.phase);
            sample += static_cast<float>(osc * envLevel * v.amplitude);
            v.phase += v.phaseIncrement;
            while (v.phase >= kTwoPi) v.phase -= kTwoPi;
            while (v.phase < 0.0) v.phase += kTwoPi;
        }

        // Mix drum synth output (render to temp buffer then add)
        std::vector<float> drumBuf(numFrames * 2);
        drumSynth_.render(drumBuf.data(), numFrames);
        for (int i = 0; i < numFrames * 2; ++i) {
            sample += drumBuf[i];
        }

        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        dry[i * 2] = sample;
        dry[i * 2 + 1] = sample;
    }

    // Copy dry to output buffer
    std::memcpy(buffer, dry.data(), numFrames * 2 * sizeof(float));

    // Apply delay effect
    delayEffect_.process(buffer, numFrames);

    // Apply reverb effect (on delayed output)
    reverbEffect_.process(buffer, numFrames);
}
