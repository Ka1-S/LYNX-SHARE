#pragma once

// A stand-in audio device for the self-tests that play the live engine without real audio hardware (round 190gh's
// export parity test, round 190hn's export stress test): callbacks of `block` samples at `rate` from a thread of its
// own, a few times faster than real time. No window, no driver.

#include <utility>

#include <juce_audio_devices/juce_audio_devices.h>

namespace lynx::standin
{
struct Device final : juce::AudioIODevice, private juce::Thread
{
    Device (double sampleRate, int blockSize, int pauseMs)
        : juce::AudioIODevice ("Stand-in device", "LYNX self-test"), juce::Thread ("stand-in device"), rate (sampleRate), block (blockSize), pause (pauseMs)
    {
    }
    ~Device() override { close(); }

    juce::StringArray getOutputChannelNames() override { return { "Out 1", "Out 2" }; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return { rate }; }
    juce::Array<int> getAvailableBufferSizes() override { return { block }; }
    int getDefaultBufferSize() override { return block; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger& outs, double, int) override
    {
        close();
        outsNow = outs;
        outsNow.setRange (2, juce::jmax (0, outsNow.getHighestBit() - 1), false);
        data.setSize (2, block);
        opened = true;
        startThread();
        return {};
    }
    void close() override
    {
        stop();
        stopThread (10000);
        opened = false;
    }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* cb) override
    {
        if (! opened || cb == nullptr)
            return;
        cb->audioDeviceAboutToStart (this);
        const juce::ScopedLock sl (callbackLock);
        callback = cb;
    }
    void stop() override
    {
        juce::AudioIODeviceCallback* old = nullptr;
        {
            const juce::ScopedLock sl (callbackLock);
            old = std::exchange (callback, nullptr);
        }
        if (old != nullptr)
            old->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outsNow; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return block; }
    int getInputLatencyInSamples() override { return 0; }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (callbackLock);
                if (callback != nullptr)
                    callback->audioDeviceIOCallbackWithContext (nullptr, 0, data.getArrayOfWritePointers(), data.getNumChannels(), block, {});
            }
            juce::Thread::sleep (pause);
        }
    }

    const double rate;
    const int block, pause;
    juce::BigInteger outsNow;
    juce::AudioBuffer<float> data;
    juce::CriticalSection callbackLock;
    juce::AudioIODeviceCallback* callback = nullptr;
    bool opened = false;
};

// the device type that lists the one device (AudioDeviceManager::addAudioDeviceType, then initialise)
struct Type final : juce::AudioIODeviceType
{
    Type (double sampleRate, int blockSize, int pauseMs = 2)
        : juce::AudioIODeviceType ("LYNX self-test"), rate (sampleRate), block (blockSize), pause (pauseMs)
    {
    }
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? juce::StringArray() : juce::StringArray ("Stand-in device"); }
    int getDefaultDeviceIndex (bool input) const override { return input ? -1 : 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool input) const override { return d != nullptr && ! input ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String&) override
    {
        return out == "Stand-in device" ? new Device (rate, block, pause) : nullptr;
    }

private:
    const double rate;
    const int block, pause;
};
} // namespace lynx::standin
