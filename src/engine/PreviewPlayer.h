#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "core/FlResampler.h"
#include "core/Instruments.h"

namespace lynx
{
struct Transport;

// Graph node that plays a file preview (sample browser), outside the song.
class PreviewPlayer final : public juce::AudioProcessor
{
public:
    PreviewPlayer();

    // UI thread. nullptr to stop. `loop`: plays it over and over (the audio editor's loop playback).
    // `editor`: the Audio Editor's playback, whose level also follows its volume knob (setEditorVolume) while it plays.
    void play (std::shared_ptr<const SampleData> sample, bool loop = false, float gain = 0.8f, bool editor = false);
    // A Browser preview: like play(), and the level also follows the preview volume (setVolume) while it plays.
    // `onBeat` (the Browser's "Preview in time", round 190aq): while the transport plays, the sample starts when the
    // next beat is heard (setTransport, setSyncLatency); with the transport stopped it starts at once.
    void playBrowser (std::shared_ptr<const SampleData> sample, float gain = 0.8f, bool onBeat = false);
    // The transport whose beats an `onBeat` preview waits for (nullptr: none, it starts at once), and how late the
    // song is heard against it (the PDC total, in samples). Set before the device runs / any thread.
    void setTransport (const Transport* t) noexcept { transport = t; }
    void setSyncLatency (int samples) noexcept { syncLatency.store (samples, std::memory_order_relaxed); }
    // How many previews started on a beat, and the offset of the last one inside its block (diagnostics, tests).
    int beatStarts() const noexcept { return beatStartCount.load (std::memory_order_relaxed); }
    // The Browser's preview volume as a gain (core/ValueMath.h previewGain). Any thread; applies at once.
    void setVolume (float gainFactor) noexcept { volume.store (gainFactor, std::memory_order_relaxed); }
    // The Audio Editor's playback volume as a gain (round 162, issue #9). Any thread; applies at once.
    void setEditorVolume (float gainFactor) noexcept { editorVolume.store (gainFactor, std::memory_order_relaxed); }
    // Any thread: the frame being played in the current sample, or -1 when nothing plays.
    double getPlayPosition() const noexcept { return playPosition.load (std::memory_order_relaxed); }

    const juce::String getName() const override { return "Preview"; }
    void prepareToPlay (double sampleRate, int) override { deviceRate = sampleRate; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::SpinLock lock;                       // the audio thread only tries (never waits)
    std::shared_ptr<const SampleData> pending; // swapped by the UI
    bool hasPending = false;
    bool pendingLoop = false, looping = false;
    float pendingGain = 0.8f, gain = 0.8f;
    bool pendingBrowser = false, browser = false;
    bool pendingOnBeat = false, waitingForBeat = false; // round 190aq: the sample waits for the next beat
    const Transport* transport = nullptr;
    std::atomic<int> syncLatency { 0 };
    std::atomic<int> beatStartCount { 0 };
    bool pendingEditor = false, editor = false;
    std::atomic<float> volume { 1.0f }, editorVolume { 1.0f };
    std::atomic<double> playPosition { -1.0 };
    std::shared_ptr<const SampleData> current;  // touched only by the audio thread
    std::shared_ptr<const SampleData> retired;  // released by the UI (audio never deallocates)
    double position = 0.0;
    double deviceRate = 44100.0;
    flrs::Kernel kernel;                       // FL's sampler interpolation (audio thread)
    std::array<float, 1024> tapL {}, tapR {};  // taps at the edges / around a loop

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreviewPlayer)
};
} // namespace lynx
