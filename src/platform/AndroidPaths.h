#pragma once

// Android-specific path handling for LYNX SHARE

#include <juce_core/juce_core.h>

namespace lynx::platform
{
#if JUCE_ANDROID || defined(LYNX_ANDROID)
juce::File androidAppDataFolder();
juce::File androidCacheFolder();
juce::File androidExternalStorageFolder();
juce::File androidDocumentsFolder();
#endif
} // namespace lynx::platform