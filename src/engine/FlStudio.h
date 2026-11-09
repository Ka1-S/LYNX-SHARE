#pragma once

#include <juce_core/juce_core.h>

#include "core/FlPluginDb.h"

namespace lynx
{
// The FL Studio installation on this PC, as FL itself records it (read-only; LYNX never writes to FL's files or keys).
struct FlStudioInstall
{
    juce::File installDir;        // e.g. C:\Program Files\Image-Line\FL Studio 2026   (%FLStudioFactoryData%)
    juce::File userData;          // e.g. Documents\Image-Line\FL Studio                (%FLStudioUserData%)
    juce::File sharedData;        // e.g. Documents\Image-Line (FL's "Shared data")     (%ILSharedData%; Downloads in it =
                                  // %FLStudioDownloadsData%)
    juce::StringArray vstFolders; // plugin folders FL is configured with (existing ones)

    bool found() const { return installDir.isDirectory() || userData.isDirectory(); }
    juce::String name() const { return installDir.isDirectory() ? installDir.getFileName() : juce::String ("FL Studio"); }
    // FL's list of the plugins it found on this PC (Presets\Plugin database\Installed)
    juce::File pluginDatabase() const { return userData.getChildFile ("Presets").getChildFile ("Plugin database").getChildFile ("Installed"); }
};

// Registry (HKCU/HKLM Software\Image-Line\Shared\Paths, ...\Plugins\FL Studio) first, then the usual folders.
FlStudioInstall findFlStudio();

// Reads FL's plugin database (.nfo and .fst files under Installed). `files` = how many files were read.
FlPluginDb readFlPluginDatabase (const FlStudioInstall& fl, int* files = nullptr);
} // namespace lynx
