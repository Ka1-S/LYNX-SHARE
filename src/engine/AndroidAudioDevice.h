// Android audio device using AAudio (API 26+) with OpenSL ES fallback

#include <juce_audio_devices/juce_audio_devices.h>

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <aaudio/AAudio.h>
#include <jni.h>
#endif

namespace lynx::engine
{
#if JUCE_ANDROID || defined(LYNX_ANDROID)
class AndroidAudioDevice : public juce::AudioIODevice
{
public:
    AndroidAudioDevice();
    ~AndroidAudioDevice() override;

    // AudioIODevice interface (JUCE 8) - exact signatures from base class
    juce::String getName() const { return deviceName; }
    int getDefaultBufferSize() override;
    int getCurrentBufferSizeSamples() override { return currentBufferSize; }
    double getCurrentSampleRate() override { return currentSampleRate; }
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;

    juce::String open(const juce::BigInteger& inputChannels,
                      const juce::BigInteger& outputChannels,
                      double sampleRate,
                      int bufferSizeSamples) override;
    void close() override;
    void start(juce::AudioIODeviceCallback* callback) override;
    void stop() override;
    bool isOpen() override { return isOpened; }
    bool isPlaying() override { return isStarted; }
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;
    int getXRunCount() const noexcept override { return xrunCount; }

    // AAudio stream callbacks
    static aaudio_data_callback_result_t onAudioCallback(AAudioStream* stream, void* userData, void* audioData, int32_t numFrames);

private:
    juce::String deviceName { "AAudio" };
    void initializeAAudio();
    void cleanupAAudio();

    AAudioStream* inputStream = nullptr;
    AAudioStream* outputStream = nullptr;
    AAudioStreamBuilder* builder = nullptr;

    juce::AudioIODeviceCallback* currentCallback = nullptr;
    bool isOpened = false;
    bool isStarted = false;
    int currentBufferSize = 256;
    double currentSampleRate = 44100.0;
    int xrunCount = 0;

    juce::AudioBuffer<float> mixingBuffer;
    int numInputChannels = 0;
    int numOutputChannels = 0;

    jobject audioTrack = nullptr;
    jobject audioRecord = nullptr;
};

#endif
} // namespace lynx::engine