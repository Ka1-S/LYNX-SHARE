#include "platform/AndroidLifecycle.h"

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <jni.h>
#include <android/log.h>

#define LOG_TAG "LYNX_Lifecycle"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lynx::platform
{

AndroidLifecycle& AndroidLifecycle::getInstance()
{
    static AndroidLifecycle instance;
    return instance;
}

void AndroidLifecycle::onStart()
{
    LOGI("onStart");
    stopped = false;
}

void AndroidLifecycle::onResume()
{
    LOGI("onResume");
    paused = false;
    for (auto& cb : resumeCallbacks)
    {
        try { cb(); } catch (...) { LOGE("Exception in resume callback"); }
    }
}

void AndroidLifecycle::onPause()
{
    LOGI("onPause");
    paused = true;
    for (auto& cb : pauseCallbacks)
    {
        try { cb(); } catch (...) { LOGE("Exception in pause callback"); }
    }
}

void AndroidLifecycle::onStop()
{
    LOGI("onStop");
    stopped = true;
    for (auto& cb : stopCallbacks)
    {
        try { cb(); } catch (...) { LOGE("Exception in stop callback"); }
    }
}

void AndroidLifecycle::onDestroy()
{
    LOGI("onDestroy");
    for (auto& cb : destroyCallbacks)
    {
        try { cb(); } catch (...) { LOGE("Exception in destroy callback"); }
    }
}

void AndroidLifecycle::onConfigurationChanged()
{
    LOGI("onConfigurationChanged");
    // Handle orientation/size changes
}

void AndroidLifecycle::onLowMemory()
{
    LOGI("onLowMemory");
    for (auto& cb : lowMemoryCallbacks)
    {
        try { cb(); } catch (...) { LOGE("Exception in low memory callback"); }
    }
}

void AndroidLifecycle::onTrimMemory(int level)
{
    LOGI("onTrimMemory: %d", level);
    // Handle memory trim based on level
}

void AndroidLifecycle::onWindowFocusChanged(bool hasFocus)
{
    LOGI("onWindowFocusChanged: %s", hasFocus ? "true" : "false");
    hasWindowFocus = hasFocus;
}

void AndroidLifecycle::onRequestPermissionsResult(int requestCode,
                                                   const juce::StringArray& permissions,
                                                   const juce::Array<int>& grantResults)
{
    LOGI("onRequestPermissionsResult: requestCode=%d", requestCode);
    // Handle permission results
    // The app can register a callback for this if needed
}

} // namespace lynx::platform

// JNI bridge implementations
extern "C" {

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeStart(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onStart();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeResume(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onResume();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativePause(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onPause();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeStop(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onStop();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeDestroy(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onDestroy();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeConfigurationChanged(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onConfigurationChanged();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeLowMemory(JNIEnv* env, jobject thiz)
{
    lynx::platform::AndroidLifecycle::getInstance().onLowMemory();
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeTrimMemory(JNIEnv* env, jobject thiz, jint level)
{
    lynx::platform::AndroidLifecycle::getInstance().onTrimMemory(level);
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeWindowFocusChanged(JNIEnv* env, jobject thiz, jboolean hasFocus)
{
    lynx::platform::AndroidLifecycle::getInstance().onWindowFocusChanged(hasFocus != JNI_FALSE);
}

JNIEXPORT void JNICALL Java_com_ka1_s_lynx_LynxActivity_onNativeRequestPermissionsResult(JNIEnv* env, jobject thiz,
                                                                                         jint requestCode,
                                                                                         jobjectArray permissions,
                                                                                         jintArray grantResults)
{
    using namespace lynx::platform;

    // Convert permissions array
    juce::StringArray permArray;
    if (permissions)
    {
        jsize len = env->GetArrayLength(permissions);
        for (jsize i = 0; i < len; ++i)
        {
            jstring permStr = (jstring)env->GetObjectArrayElement(permissions, i);
            if (permStr)
            {
                const char* chars = env->GetStringUTFChars(permStr, nullptr);
                if (chars)
                {
                    permArray.add(juce::String(chars));
                    env->ReleaseStringUTFChars(permStr, chars);
                }
                env->DeleteLocalRef(permStr);
            }
        }
    }

    // Convert grant results array
    juce::Array<int> resultsArray;
    if (grantResults)
    {
        jsize len = env->GetArrayLength(grantResults);
        jint* elements = env->GetIntArrayElements(grantResults, nullptr);
        if (elements)
        {
            for (jsize i = 0; i < len; ++i)
            {
                resultsArray.add(elements[i]);
            }
            env->ReleaseIntArrayElements(grantResults, elements, JNI_ABORT);
        }
    }

    AndroidLifecycle::getInstance().onRequestPermissionsResult(requestCode, permArray, resultsArray);
}

}
#endif