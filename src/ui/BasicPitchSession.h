#pragma once

// Spotify's Basic Pitch (ICASSP 2022, Apache-2.0; third_party/basic-pitch) on the ONNX Runtime LYNX ships (round
// a2m189b; a module of its own since round 190lj so the polyphonic editor can reuse it). Nothing of it is in memory
// until a Session opens; a closed Session leaves no thread or buffer behind. Implemented in ui/AudioToMidi.cpp.
//
// Light by design: the 230 KB graph is compiled in, the runtime is the one stem extraction already loads (no extra
// download or library), the audio is brought to 22.05 kHz by a polyphase table (round 190lj: 2.2 s of CPU per minute of
// audio in all, was 7.9 s with the per-sample sinc), and the intra-op threads leave cores free for the audio callback
// and the UI. Session::run takes several windows at once (the graph's batch axis) for callers that want it.

#include <functional>
#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/AudioToMidi.h"

namespace lynx::basicpitch
{
class Session
{
public:
    Session();
    ~Session();

    // Loads ONNX Runtime and the model; false with a translated reason (runtime missing / not loadable).
    bool open (juce::String& error);

    // `count` windows back to back: `in` = count x a2m::windowSamples samples at 22.05 kHz; note / onset = count x
    // framesPerWindow x a2m::keys each; contour (null: not computed out) count x framesPerWindow x a2m::contourBins.
    bool run (const float* in, int count, float* note, float* onset, float* contour, juce::String& error);

    // Any thread: ends a running window early (run returns false).
    void terminate();

    static constexpr int framesPerWindow = 172; // the graph's output frames per 2 s window

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// The posteriorgrams of mono audio at any rate (core a2m::toModelRate, then a2m::runModelBatched): frame k at
// k * 256 / 22050 s. False on cancel or error (`error` says which). `seconds` (may be null) gets {resample, model}.
bool posteriorgram (Session& session, const std::vector<float>& mono, double rate, bool withContour,
                    const std::function<bool()>& cancelled, const std::function<void (double)>& progress, a2m::Posteriorgram& out,
                    juce::String& error, double* seconds = nullptr);
} // namespace lynx::basicpitch
