#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MissingFiles.h"

namespace lynx
{
// What the "Missing files" window shows for an opened project (core/MissingFiles.h).
struct MissingFilesInfo
{
    std::vector<MissingFile> files;     // samples not found
    std::vector<MissingPlugin> plugins; // plugins not installed here, with what replaced them
    std::vector<std::pair<std::string, std::string>> autoRelocated; // found elsewhere when opening: saved path → used path
    juce::String projectName;
    juce::StringArray searchFolders; // also searched when the window opens (the project's folder), not remembered
};

// Search folders the user picked in this window before (most recent first), kept in the settings folder; the
// Browser's user folders are searched too. The window searches them by itself when it opens.
std::vector<std::string> loadSearchFolders();
void saveSearchFolders (const std::vector<std::string>& folders);
juce::StringArray browserSearchFolders(); // the Browser's user folders (browser-folders.txt)

// Opens the window (non-modal, above the main window). `onApply(saved path → new path)` runs when the user applies
// the files found; `onClosed` runs when the window goes away (applied or not).
void showMissingFilesDialog (MissingFilesInfo info, std::function<void (const std::map<std::string, std::string>&)> onApply,
                             std::function<void()> onClosed = {});

// The window's content, for --snapshot mode (no automatic search, nothing saved): `page` 0 samples, 1 plugins,
// 2 found automatically; `selectedRow` ≥ 0 selects a row.
std::unique_ptr<juce::Component> makeMissingFilesPanel (MissingFilesInfo info, int page = 0, int selectedRow = -1);

// (shots, round 190eo) the samples page with a real search of `folders` running for the files of `info` (issue #159:
// matches stream in while it walks); `missingFilesSearchState` = true while that search still runs, with the status line
// and whether Stop / Apply are shown; `stopMissingFilesSearch` lets the search go (it ends at its next entry).
std::unique_ptr<juce::Component> makeMissingFilesPanelSearching (MissingFilesInfo info, const juce::StringArray& folders);
bool missingFilesSearchState (juce::Component& panel, juce::String& status, bool& stopShown, bool& applyEnabled);
void stopMissingFilesSearch (juce::Component& panel);

// Sample content for the snapshots: a project with missing, located, skipped and relocated files and missing plugins.
MissingFilesInfo missingFilesSnapshotInfo();
} // namespace lynx
