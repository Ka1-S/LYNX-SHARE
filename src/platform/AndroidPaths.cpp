#include "platform/AndroidPaths.h"

#include <juce_core/juce_core.h>

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <android/native_activity.h>
#include <android/asset_manager.h>
// android_native_app_glue.h is provided via CMake include path (-I${ANDROID_NDK}/sources/android/native_app_glue)
#include "android_native_app_glue.h"
#include <jni.h>

extern "C" {
    extern JavaVM* android_jvm;
    extern struct android_app* android_app;
}

namespace lynx::platform
{

juce::File androidAppDataFolder()
{
    // Returns the internal app data directory (/data/data/com.ka1-s.lynx/files)
    // This is the equivalent of settingsFolder() on other platforms
    if (android_app && android_app->activity && android_app->activity->internalDataPath)
    {
        return juce::File(android_app->activity->internalDataPath);
    }
    return juce::File();
}

juce::File androidCacheFolder()
{
    // Returns the cache directory (/data/data/com.ka1-s.lynx/cache)
    // ANativeActivity doesn't have cacheDir member, so we get it via JNI from Context.getCacheDir()
    if (android_app && android_app->activity)
    {
        JNIEnv* env = android_app->activity->env;
        jobject activity = android_app->activity->clazz;
        if (env && activity)
        {
            // Get Context class and getCacheDir() method
            jclass contextClass = env->GetObjectClass(activity);
            if (contextClass)
            {
                jmethodID getCacheDirMethod = env->GetMethodID(contextClass, "getCacheDir", "()Ljava/io/File;");
                if (getCacheDirMethod)
                {
                    jobject cacheDirFile = env->CallObjectMethod(activity, getCacheDirMethod);
                    if (cacheDirFile)
                    {
                        // Get absolute path from File object
                        jclass fileClass = env->GetObjectClass(cacheDirFile);
                        if (fileClass)
                        {
                            jmethodID getAbsolutePathMethod = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
                            if (getAbsolutePathMethod)
                            {
                                jstring pathString = (jstring)env->CallObjectMethod(cacheDirFile, getAbsolutePathMethod);
                                if (pathString)
                                {
                                    const char* pathChars = env->GetStringUTFChars(pathString, nullptr);
                                    if (pathChars)
                                    {
                                        juce::File result(pathChars);
                                        env->ReleaseStringUTFChars(pathString, pathChars);
                                        env->DeleteLocalRef(pathString);
                                        env->DeleteLocalRef(fileClass);
                                        env->DeleteLocalRef(cacheDirFile);
                                        env->DeleteLocalRef(contextClass);
                                        return result;
                                    }
                                    env->DeleteLocalRef(pathString);
                                }
                            }
                            env->DeleteLocalRef(fileClass);
                        }
                        env->DeleteLocalRef(cacheDirFile);
                    }
                }
                env->DeleteLocalRef(contextClass);
            }
        }
    }
    return juce::File();
}

juce::File androidExternalStorageFolder()
{
    // Returns the external storage directory (may require permissions)
    // This is the equivalent of documentsFolder() on other platforms
    if (android_app && android_app->activity && android_app->activity->externalDataPath)
    {
        return juce::File(android_app->activity->externalDataPath);
    }
    // Fallback to internal storage if external is not available
    return androidAppDataFolder();
}

juce::File androidDocumentsFolder()
{
    // On Android 10+ (API 29+), use scoped storage
    // For now, return external storage or internal app data as fallback
    auto external = androidExternalStorageFolder();
    if (external.exists() || external.createDirectory())
        return external;
    return androidAppDataFolder();
}

#endif
} // namespace lynx::platform