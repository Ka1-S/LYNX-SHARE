/**
 * Android JNI glue for LYNX SHARE
 * This file provides the JNI_OnLoad and native activity glue
 */

#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>

#define LOG_TAG "LYNX_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global JavaVM pointer
JavaVM* android_jvm = nullptr;
// Global android_app pointer (for native_app_glue)
struct android_app* g_android_app = nullptr;

extern "C" {

// JNI_OnLoad - called when the library is loaded
jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    android_jvm = vm;
    LOGI("JNI_OnLoad");
    return JNI_VERSION_1_6;
}

// JNI_OnUnload - called when the library is unloaded
void JNI_OnUnload(JavaVM* vm, void* reserved)
{
    LOGI("JNI_OnUnload");
    android_jvm = nullptr;
}

// Native activity callbacks (for native_app_glue compatibility)
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize)
{
    LOGI("ANativeActivity_onCreate");
    // Store the activity pointer for later use
    g_android_app = reinterpret_cast<struct android_app*>(activity->instance);
    if (g_android_app)
    {
        g_android_app->activity = activity;
    }
}

void ANativeActivity_onStart(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onStart");
}

void ANativeActivity_onResume(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onResume");
}

void ANativeActivity_onPause(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onPause");
}

void ANativeActivity_onStop(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onStop");
}

void ANativeActivity_onDestroy(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onDestroy");
}

void ANativeActivity_onWindowFocusChanged(ANativeActivity* activity, int focused)
{
    LOGI("ANativeActivity_onWindowFocusChanged: %d", focused);
}

void ANativeActivity_onNativeWindowCreated(ANativeActivity* activity, ANativeWindow* window)
{
    LOGI("ANativeActivity_onNativeWindowCreated");
}

void ANativeActivity_onNativeWindowDestroyed(ANativeActivity* activity, ANativeWindow* window)
{
    LOGI("ANativeActivity_onNativeWindowDestroyed");
}

void ANativeActivity_onInputQueueCreated(ANativeActivity* activity, AInputQueue* queue)
{
    LOGI("ANativeActivity_onInputQueueCreated");
}

void ANativeActivity_onInputQueueDestroyed(ANativeActivity* activity, AInputQueue* queue)
{
    LOGI("ANativeActivity_onInputQueueDestroyed");
}

void ANativeActivity_onConfigurationChanged(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onConfigurationChanged");
}

void ANativeActivity_onLowMemory(ANativeActivity* activity)
{
    LOGI("ANativeActivity_onLowMemory");
}

} // extern "C"
