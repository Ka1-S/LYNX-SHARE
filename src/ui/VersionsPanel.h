#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ProjectSafety.h"

namespace lynx
{
// The Versions panel (round 190ap): one list of the open project's saved versions ("Save new version": Song, Song_2,
// Song_3... next to the project) and of its backups (the timed autosaves and the copies a save replaced, in the
// backup folder), newest first, with date, size and an optional label. It only reads what LYNX already writes: the
// files' names, format and place are untouched, and the labels are kept in "<name>.version-labels.txt" beside them.
struct VersionsContext
{
    juce::File projectFile;    // the open project's file; File() while it was never saved (or was imported)
    juce::String projectStem;  // the name its backups are filed under
    juce::File backupFolder;
    juce::String extension;    // ".lynx"
};

struct VersionsActions
{
    std::function<VersionsContext()> context;
    // "Open as a copy": the file opens as an unsaved project named `name`, so the file itself is never written over
    std::function<void (const juce::File&, const juce::String& name)> openAsCopy;
    std::function<void()> saveNewVersion; // File › Save new version
};

std::unique_ptr<juce::Component> makeVersionsPanel (VersionsActions actions);

// What the panel lists for `context` (also for the tests of the files on disk): the entries, newest first.
std::vector<safety::versions::Entry> versionEntries (const VersionsContext& context);
// The file of an entry: next to the project (a version) or in the backup folder.
juce::File versionFile (const VersionsContext& context, const safety::versions::Entry& entry);
// Sets (or, with an empty text, removes) an entry's label in the labels file beside it. False if it can't be written.
bool setVersionLabel (const VersionsContext& context, const safety::versions::Entry& entry, const juce::String& label);
// The labels of the project's backups (the rotation leaves labelled backups alone).
safety::versions::Labels backupLabels (const juce::File& backupFolder, const juce::String& projectStem);

// "just now", "5 min ago", "3 h ago", "12 d ago"
juce::String versionAgo (juce::int64 fileTimeMs, juce::int64 nowMs);

// --selftest: versions and backups written to a temporary folder are listed, labelled, relabelled and the labels
// survive on disk beside the files, which stay byte for byte as they were. "OK" or what failed.
juce::String versionsSelfTest();
} // namespace lynx
