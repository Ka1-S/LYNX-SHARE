#include "aaudio_output.h"

#include <android/log.h>
#include <dlfcn.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "LYNX_AAUDIO", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LYNX_AAUDIO", __VA_ARGS__)

// AAudio function pointer types (AAudio.h from NDK)
typedef int32_t (*AAudioStreamBuilder_setSampleRate)(void*, int32_t);
typedef int32_t (*AAudioStreamBuilder_setChannelCount)(void*, int32_t);
typedef int32_t (*AAudioStreamBuilder_setFormat)(void*, int32_t);
typedef int32_t (*AAudioStreamBuilder_setSharingMode)(void*, int32_t);
typedef int32_t (*AAudioStreamBuilder_setPerformanceMode)(void*, int32_t);
typedef int32_t (*AAudioStreamBuilder_setDataCallback)(void*, int32_t (*)(void*, void*, void*, int32_t), void*);
typedef int32_t (*AAudioStreamBuilder_openStream)(void*, void**);
typedef void (*AAudioStreamBuilder_delete)(void*);
typedef void* (*AAudio_createStreamBuilder)();
typedef int32_t (*AAudioStream_requestStart)(void*);
typedef int32_t (*AAudioStream_requestStop)(void*);
typedef int32_t (*AAudioStream_close)(void*);
typedef int32_t (*AAudioStream_getSampleRate)(void*);
typedef int32_t (*AAudioStream_getFramesPerBurst)(void*);

struct AAudioOutput::Impl {
    void* libHandle = nullptr;

    AAudio_createStreamBuilder createStreamBuilder = nullptr;
    AAudioStreamBuilder_delete deleteBuilder = nullptr;
    AAudioStreamBuilder_setSampleRate builderSetSampleRate = nullptr;
    AAudioStreamBuilder_setChannelCount builderSetChannelCount = nullptr;
    AAudioStreamBuilder_setFormat builderSetFormat = nullptr;
    AAudioStreamBuilder_setSharingMode builderSetSharingMode = nullptr;
    AAudioStreamBuilder_setPerformanceMode builderSetPerfMode = nullptr;
    AAudioStreamBuilder_setDataCallback builderSetCallback = nullptr;
    AAudioStreamBuilder_openStream builderOpenStream = nullptr;

    AAudioStream_requestStart streamStart = nullptr;
    AAudioStream_requestStop streamStop = nullptr;
    AAudioStream_close streamClose = nullptr;
    AAudioStream_getSampleRate streamGetSampleRate = nullptr;
    AAudioStream_getFramesPerBurst streamGetFramesPerBurst = nullptr;

    void* stream = nullptr;
    std::atomic<bool> running{false};
    std::function<void(float*, int)> renderCallback;
    int sampleRate = 44100;
    int framesPerBurst = 512;
};

// We store the Impl pointer in thread-local so dataCallback can reach it.
static thread_local AAudioOutput::Impl* g_currentImpl = nullptr;

static int32_t realDataCallback(void* /*cookie*/, void* audioData, void* /*cookie2*/, int32_t numFrames) {
    if (!g_currentImpl || !g_currentImpl->renderCallback) {
        std::memset(audioData, 0, static_cast<size_t>(numFrames) * 2 * sizeof(float));
        return 0; // AAUDIO_CALLBACK_RESULT_CONTINUE
    }
    float* buffer = static_cast<float*>(audioData);
    g_currentImpl->renderCallback(buffer, numFrames);
    return 0;
}

AAudioOutput::AAudioOutput() = default;

AAudioOutput::~AAudioOutput() {
    stop();
    if (impl_->libHandle) {
        dlclose(impl_->libHandle);
    }
    delete impl_;
}

bool AAudioOutput::start(int sampleRate, int framesPerBurst,
                         std::function<void(float*, int)> renderCallback) {
    if (impl_->running.load()) {
        return true;
    }

    impl_->libHandle = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!impl_->libHandle) {
        LOGE("Cannot load libaaudio.so: %s", dlerror());
        return false;
    }

    impl_->createStreamBuilder = reinterpret_cast<AAudio_createStreamBuilder>(
        dlsym(impl_->libHandle, "AAudio_createStreamBuilder"));
    impl_->deleteBuilder = reinterpret_cast<AAudioStreamBuilder_delete>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_delete"));
    impl_->builderSetSampleRate = reinterpret_cast<AAudioStreamBuilder_setSampleRate>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setSampleRate"));
    impl_->builderSetChannelCount = reinterpret_cast<AAudioStreamBuilder_setChannelCount>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setChannelCount"));
    impl_->builderSetFormat = reinterpret_cast<AAudioStreamBuilder_setFormat>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setFormat"));
    impl_->builderSetSharingMode = reinterpret_cast<AAudioStreamBuilder_setSharingMode>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setSharingMode"));
    impl_->builderSetPerfMode = reinterpret_cast<AAudioStreamBuilder_setPerformanceMode>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setPerformanceMode"));
    impl_->builderSetCallback = reinterpret_cast<AAudioStreamBuilder_setDataCallback>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_setDataCallback"));
    impl_->builderOpenStream = reinterpret_cast<AAudioStreamBuilder_openStream>(
        dlsym(impl_->libHandle, "AAudioStreamBuilder_openStream"));

    impl_->streamStart = reinterpret_cast<AAudioStream_requestStart>(
        dlsym(impl_->libHandle, "AAudioStream_requestStart"));
    impl_->streamStop = reinterpret_cast<AAudioStream_requestStop>(
        dlsym(impl_->libHandle, "AAudioStream_requestStop"));
    impl_->streamClose = reinterpret_cast<AAudioStream_close>(
        dlsym(impl_->libHandle, "AAudioStream_close"));
    impl_->streamGetSampleRate = reinterpret_cast<AAudioStream_getSampleRate>(
        dlsym(impl_->libHandle, "AAudioStream_getSampleRate"));
    impl_->streamGetFramesPerBurst = reinterpret_cast<AAudioStream_getFramesPerBurst>(
        dlsym(impl_->libHandle, "AAudioStream_getFramesPerBurst"));

    if (!impl_->createStreamBuilder || !impl_->builderOpenStream || !impl_->streamStart) {
        LOGE("Missing AAudio symbols");
        return false;
    }

    impl_->sampleRate = sampleRate;
    impl_->framesPerBurst = framesPerBurst;
    impl_->renderCallback = std::move(renderCallback);
    impl_->running.store(true);

    void* builder = impl_->createStreamBuilder();
    if (!builder) {
        LOGE("AAudio_createStreamBuilder returned null");
        return false;
    }

    impl_->builderSetSampleRate(builder, sampleRate);
    impl_->builderSetChannelCount(builder, 2); // stereo
    impl_->builderSetFormat(builder, 7); // AAUDIO_FORMAT_FLOAT_IEEE754
    impl_->builderSetSharingMode(builder, 1); // AAUDIO_SHARING_MODE_SHARED
    impl_->builderSetPerfMode(builder, 3); // AAUDIO_PERFORMANCE_MODE_LOW_LATENCY

    // Set user data via the data callback wrapper that captures impl
    g_currentImpl = impl_;
    impl_->builderSetCallback(builder, realDataCallback, impl_);

    int32_t result = impl_->builderOpenStream(builder, &impl_->stream);
    impl_->deleteBuilder(builder);

    if (result != 0 || !impl_->stream) {
        LOGE("AAudioStreamBuilder_openStream failed: %d", result);
        impl_->running.store(false);
        return false;
    }

    result = impl_->streamStart(impl_->stream);
    if (result != 0) {
        LOGE("AAudioStream_requestStart failed: %d", result);
        impl_->streamClose(impl_->stream);
        impl_->stream = nullptr;
        impl_->running.store(false);
        return false;
    }

    LOGI("AAudio started: %d Hz, %d frames/burst", sampleRate, framesPerBurst);
    return true;
}

void AAudioOutput::stop() {
    if (!impl_->running.load()) {
        return;
    }
    impl_->running.store(false);
    if (impl_->stream) {
        impl_->streamStop(impl_->stream);
        impl_->streamClose(impl_->stream);
        impl_->stream = nullptr;
    }
    g_currentImpl = nullptr;
    LOGI("AAudio stopped");
}

bool AAudioOutput::isRunning() const {
    return impl_->running.load();
}
