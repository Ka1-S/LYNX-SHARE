#pragma once

#include <functional>

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

    // how the installation was found (round 190rh, issue #291: Settings › Files › FL Studio folder)
    enum class Source { none, variable, setting, detected };
    Source source = Source::none;

    bool found() const { return installDir.isDirectory() || userData.isDirectory(); }
    juce::String name() const { return installDir.isDirectory() ? installDir.getFileName() : juce::String ("FL Studio"); }
    // FL's list of the plugins it found on this PC (Presets\Plugin database\Installed)
    juce::File pluginDatabase() const { return userData.getChildFile ("Presets").getChildFile ("Plugin database").getChildFile ("Installed"); }
};

// LYNX_FL_INSTALL first, then Settings › Files › FL Studio folder, then the registry (HKCU/HKLM
// Software\Image-Line\Shared\Paths, ...\Plugins\FL Studio), then the usual folders. A variable or setting whose folder
// is not there (a drive that is not mounted) is passed over, not forgotten.
//
// The places are looked at by a worker, so a hung or slow drive (a network mount, a disk spinning up) never holds the
// window: a caller waits at most `waitMs` and then goes on without FL; the answer is used when it comes (flStudioChanged).
// Asked again, the last answer comes at once and is refreshed in the background. An import, whose result depends on FL's
// files, waits importWaitMs.
constexpr int flStudioWaitMs = 800, flStudioImportWaitMs = 15000;
FlStudioInstall findFlStudio (int waitMs = flStudioWaitMs);
// Set by the main window: called on the message thread when the installation changed after the fact (a late answer, a
// drive mounted meanwhile); flStudioAccess() is already the new one.
inline std::function<void()> flStudioChanged;

// Settings › Files › FL Studio folder (round 190rh): the saved folder ("" = none), in <settings>/fl-studio-folder.txt.
// setFlFolderSetting saves it ({} clears it) and makes flStudioAccess() find the installation again.
juce::String flFolderSetting();
bool setFlFolderSetting (const juce::File& folder);
// the folder LYNX_FL_INSTALL names ("" = the variable is not set)
juce::String flFolderVariable();
// the installations Detect finds: the usual places and the mounted drives (may wait for a slow drive: not on the message thread)
juce::StringArray detectFlFolders();

// Reads FL's plugin database (.nfo and .fst files under Installed). `files` = how many files were read.
FlPluginDb readFlPluginDatabase (const FlStudioInstall& fl, int* files = nullptr);
} // namespace lynx
