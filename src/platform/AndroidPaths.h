#pragma once

// Android-specific path handling for LYNX SHARE
// Include inside namespace lynx::platform

#include <juce_core/juce_core.h>

#if JUCE_ANDROID || defined(LYNX_ANDROID)
juce::File androidAppDataFolder();
juce::File androidCacheFolder();
juce::File androidExternalStorageFolder();
juce::File androidDocumentsFolder();
#endif