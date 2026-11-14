#pragma once

// Android lifecycle handling for LYNX SHARE

#include <juce_core/juce_core.h>

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <jni.h>
#include <android/native_activity.h>
#endif

namespace lynx::platform
{
#if JUCE_ANDROID || defined(LYNX_ANDROID)

class AndroidLifecycle
{
public:
    static AndroidLifecycle& getInstance();

    // Called from Java side via JNI
    void onStart();
    void onResume();
    void onPause();
    void onStop();
    void onDestroy();
    void onConfigurationChanged();
    void onLowMemory();
    void onTrimMemory(int level);
    void onWindowFocusChanged(bool hasFocus);
    void onRequestPermissionsResult(int requestCode, const juce::StringArray& permissions, const juce::Array<int>& grantResults);

    // Query state
    bool isPaused() const { return paused; }
    bool isStopped() const { return stopped; }
    bool hasFocus() const { return hasWindowFocus; }

    // Callbacks for the app to register
    using LifecycleCallback = std::function<void()>;
    void addPauseCallback(LifecycleCallback cb) { pauseCallbacks.push_back(std::move(cb)); }
    void addResumeCallback(LifecycleCallback cb) { resumeCallbacks.push_back(std::move(cb)); }
    void addStopCallback(LifecycleCallback cb) { stopCallbacks.push_back(std::move(cb)); }
    void addDestroyCallback(LifecycleCallback cb) { destroyCallbacks.push_back(std::move(cb)); }
    void addLowMemoryCallback(LifecycleCallback cb) { lowMemoryCallbacks.push_back(std::move(cb)); }

private:
    AndroidLifecycle() = default;
    ~AndroidLifecycle() = default;

    bool paused = false;
    bool stopped = false;
    bool hasWindowFocus = true;

    std::vector<LifecycleCallback> pauseCallbacks;
    std::vector<LifecycleCallback> resumeCallbacks;
    std::vector<LifecycleCallback> stopCallbacks;
    std::vector<LifecycleCallback> destroyCallbacks;
    std::vector<LifecycleCallback> lowMemoryCallbacks;
};

// JNI bridge functions called from Java
extern "C" {

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeStart(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeResume(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativePause(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeStop(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeDestroy(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeConfigurationChanged(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeLowMemory(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeTrimMemory(JNIEnv* env, jobject thiz, jint level);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeWindowFocusChanged(JNIEnv* env, jobject thiz, jboolean hasFocus);
JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeRequestPermissionsResult(JNIEnv* env, jobject thiz,
                                                                                          jint requestCode,
                                                                                          jobjectArray permissions,
                                                                                          jintArray grantResults);

}

#endif
} // namespace lynx::platform
