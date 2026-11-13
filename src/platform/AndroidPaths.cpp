#include "platform/AndroidPaths.h"

#include <juce_core/juce_core.h>

#if JUCE_ANDROID || defined(LYNX_ANDROID)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <jni.h>

// native_app_glue.h is provided by JUCE's juce_android_native_app_glue module
// Include via the JUCE module path
#include <juce_audio_devices/native/juce_android_AndroidApp.h>

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
    if (android_app && android_app->activity && android_app->activity->cacheDir)
    {
        return juce::File(android_app->activity->cacheDir);
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