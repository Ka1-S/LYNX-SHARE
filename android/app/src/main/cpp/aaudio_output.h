#pragma once

#include <cstdint>
#include <functional>

// AAudio output wrapper — minimal, JUCE-free.
// Loads libaaudio dynamically via dlopen so the app runs on devices
// without AAudio (OpenSL ES fallback would go here in a full build).
class AAudioOutput {
public:
    AAudioOutput();
    ~AAudioOutput();

    bool start(int sampleRate, int framesPerBurst,
               std::function<void(float*, int)> renderCallback);
    void stop();
    bool isRunning() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
