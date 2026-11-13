#include "engine/AndroidAudioDevice.h"

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <aaudio/AAudio.h>

extern "C" {
    extern JavaVM* android_jvm;
}

#define LOG_TAG "LYNX_Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lynx::engine
{

AndroidAudioDevice::AndroidAudioDevice() : juce::AudioIODevice("AAudio", "AAudio")
{
    AAudio_createStreamBuilder(&builder);
}

AndroidAudioDevice::~AndroidAudioDevice()
{
    close();
    if (builder)
    {
        AAudioStreamBuilder_delete(builder);
        builder = nullptr;
    }
}

int AndroidAudioDevice::getDefaultBufferSize()
{
    if (builder)
    {
        int32_t framesPerBurst = AAudioStreamBuilder_getFramesPerBurst(builder);
        if (framesPerBurst > 0)
            return framesPerBurst;
    }
    return 256;
}

juce::Array<double> AndroidAudioDevice::getAvailableSampleRates()
{
    juce::Array<double> rates;
    rates.add(44100.0);
    rates.add(48000.0);
    return rates;
}

juce::Array<int> AndroidAudioDevice::getAvailableBufferSizes()
{
    juce::Array<int> sizes;
    int defaultSize = getDefaultBufferSize();
    sizes.add(defaultSize);
    sizes.add(defaultSize * 2);
    sizes.add(defaultSize * 4);
    return sizes;
}

bool AndroidAudioDevice::open(const juce::BigInteger& inputChannels,
                              const juce::BigInteger& outputChannels,
                              double sampleRate,
                              int bufferSizeSamples)
{
    if (isOpened)
        return true;

    numInputChannels = inputChannels.countNumberOfSetBits();
    numOutputChannels = outputChannels.countNumberOfSetBits();
    currentSampleRate = sampleRate;
    currentBufferSize = bufferSizeSamples > 0 ? bufferSizeSamples : getDefaultBufferSize();

    // Create output stream
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(sampleRate));
    AAudioStreamBuilder_setChannelCount(builder, numOutputChannels);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setBufferCapacityInFrames(builder, currentBufferSize * 4);
    AAudioStreamBuilder_setDataCallback(builder, onAudioCallback, this);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &outputStream);
    if (result != AAUDIO_OK)
    {
        LOGE("Failed to open output stream: %s", AAudio_convertResultToText(result));
        return false;
    }

    if (numInputChannels > 0)
    {
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setChannelCount(builder, numInputChannels);
        result = AAudioStreamBuilder_openStream(builder, &inputStream);
        if (result != AAUDIO_OK)
        {
            LOGE("Failed to open input stream: %s", AAudio_convertResultToText(result));
            AAudioStream_close(outputStream);
            outputStream = nullptr;
            return false;
        }
    }

    currentBufferSize = AAudioStream_getBufferSizeInFrames(outputStream);
    currentSampleRate = AAudioStream_getSampleRate(outputStream);

    mixingBuffer.setSize(juce::jmax(numInputChannels, numOutputChannels), currentBufferSize);

    isOpened = true;
    LOGI("AAudio opened: %d Hz, %d frames, %d in, %d out",
         (int)currentSampleRate, currentBufferSize, numInputChannels, numOutputChannels);

    return true;
}

void AndroidAudioDevice::close()
{
    if (!isOpened)
        return;

    stop();

    if (outputStream)
    {
        AAudioStream_close(outputStream);
        outputStream = nullptr;
    }
    if (inputStream)
    {
        AAudioStream_close(inputStream);
        inputStream = nullptr;
    }

    isOpened = false;
    currentCallback = nullptr;
    LOGI("AAudio closed");
}

void AndroidAudioDevice::start(juce::AudioIODeviceCallback* callback)
{
    if (!isOpened || isStarted || !callback)
        return;

    currentCallback = callback;
    callback->audioDeviceAboutToStart(this);

    aaudio_result_t result = AAudioStream_requestStart(outputStream);
    if (result != AAUDIO_OK)
    {
        LOGE("Failed to start output stream: %s", AAudio_convertResultToText(result));
        callback->audioDeviceStopped();
        currentCallback = nullptr;
        return;
    }

    if (inputStream)
    {
        result = AAudioStream_requestStart(inputStream);
        if (result != AAUDIO_OK)
        {
            LOGE("Failed to start input stream: %s", AAudio_convertResultToText(result));
        }
    }

    isStarted = true;
    LOGI("AAudio started");
}

void AndroidAudioDevice::stop()
{
    if (!isStarted)
        return;

    if (outputStream)
    {
        AAudioStream_requestStop(outputStream);
    }
    if (inputStream)
    {
        AAudioStream_requestStop(inputStream);
    }

    if (currentCallback)
    {
        currentCallback->audioDeviceStopped();
        currentCallback = nullptr;
    }

    isStarted = false;
    LOGI("AAudio stopped");
}

int AndroidAudioDevice::getOutputLatencyInSamples()
{
    if (outputStream)
    {
        return AAudioStream_getFramesPerBurst(outputStream) * 2;
    }
    return currentBufferSize;
}

int AndroidAudioDevice::getInputLatencyInSamples()
{
    if (inputStream)
    {
        return AAudioStream_getFramesPerBurst(inputStream) * 2;
    }
    return currentBufferSize;
}

aaudio_data_callback_result_t AndroidAudioDevice::onAudioCallback(AAudioStream* stream,
                                                                    void* userData,
                                                                    void* audioData,
                                                                    int32_t numFrames)
{
    auto* device = static_cast<AndroidAudioDevice*>(userData);
    if (!device || !device->currentCallback || !device->isStarted)
        return AAUDIO_CALLBACK_RESULT_CONTINUE;

    float* outputBuffer = static_cast<float*>(audioData);
    const int numOutChannels = device->numOutputChannels;
    const int numInChannels = device->numInputChannels;

    device->mixingBuffer.clear();

    if (device->inputStream && numInChannels > 0)
    {
        aaudio_result_t result = AAudioStream_read(device->inputStream,
                                                   device->mixingBuffer.getArrayOfWritePointers(),
                                                   numFrames,
                                                   0);
        if (result < 0)
        {
            device->xrunCount++;
        }
    }

    device->currentCallback->audioDeviceIOCallbackWithContext(
        device->mixingBuffer.getArrayOfReadPointers(), numInChannels,
        device->mixingBuffer.getArrayOfWritePointers(), numOutChannels,
        numFrames, nullptr);

    for (int ch = 0; ch < numOutChannels; ++ch)
    {
        const float* src = device->mixingBuffer.getReadPointer(ch);
        float* dst = outputBuffer + ch;
        for (int i = 0; i < numFrames; ++i)
        {
            dst[i * numOutChannels] = src[i];
        }
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace lynx::engine

#endif // JUCE_ANDROID || defined(LYNX_ANDROID)